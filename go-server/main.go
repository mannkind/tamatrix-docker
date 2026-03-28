/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Jeroen Domburg <jeroen@spritesmods.com> wrote this file. As long as you retain
 * this notice you can do whatever you want with this stuff. If we meet some day,
 * and you think this stuff is worth it, you can buy me a beer in return.
 * ----------------------------------------------------------------------------
 *
 * Go reimplementation of server/main.c — combines the original tamaserver
 * (UDP broker + SHM writer) with the HTTP/static-file server into one binary.
 */

package main

import (
	"encoding/binary"
	"encoding/json"
	"flag"
	"log"
	"math/rand"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"strconv"
	"sync"

	"golang.org/x/sys/unix"
)

// SHM layout (packed structs, key 7531):
//
//	ShmData {
//	    uint32 currSeq        // offset 0
//	    uint32 noTamas        // offset 4
//	    TamaDisp tama[128]    // offset 8
//	}
//	TamaDisp {
//	    uint32 lastSeq        // +0  (0xFFFFFFFF = unused slot)
//	    uint8  display[1536]  // +4
//	    uint16 icons          // +1540
//	}                         // stride: 1542
const (
	shmKey     = 7531
	maxTamas   = 128
	dispSize   = 32 * 48 // 1536
	tamaStride = 4 + dispSize + 2
	shmTotal   = 8 + maxTamas*tamaStride

	unusedSeq = uint32(0xFFFFFFFF) // sentinel: slot is free

	pktImage      = 0
	pktIRStart    = 1
	pktIRStartAck = 2
	pktIRData     = 3
	pktBye        = 4
	pktBtn        = 5
)

var (
	shm     []byte
	shmMu   sync.RWMutex
	udpConn *net.UDPConn
)

// client tracks per-emulator state that lives outside SHM.
type client struct {
	addr        *net.UDPAddr
	connectedTo int // slot index of current IR peer, -1 if none
}

var clients [maxTamas]*client

// --- SHM accessors ---

func shmCurrSeq() uint32 {
	return binary.LittleEndian.Uint32(shm[0:4])
}

func shmSetCurrSeq(v uint32) {
	binary.LittleEndian.PutUint32(shm[0:4], v)
}

func shmNoTamas() uint32 {
	return binary.LittleEndian.Uint32(shm[4:8])
}

func shmSetNoTamas(v uint32) {
	binary.LittleEndian.PutUint32(shm[4:8], v)
}

func tamaOffset(i int) int {
	return 8 + i*tamaStride
}

func shmLastSeq(i int) uint32 {
	off := tamaOffset(i)
	return binary.LittleEndian.Uint32(shm[off : off+4])
}

func shmSetLastSeq(i int, v uint32) {
	off := tamaOffset(i)
	binary.LittleEndian.PutUint32(shm[off:off+4], v)
}

// --- UDP server ---

func runUDP(addr string) {
	uaddr, err := net.ResolveUDPAddr("udp", addr)
	if err != nil {
		log.Fatalf("resolve UDP addr: %v", err)
	}
	conn, err := net.ListenUDP("udp", uaddr)
	if err != nil {
		log.Fatalf("listen UDP %s: %v", addr, err)
	}
	udpConn = conn
	defer conn.Close()
	log.Printf("UDP server listening on %s", addr)

	buf := make([]byte, 2048)
	for {
		n, src, err := conn.ReadFromUDP(buf)
		if err != nil {
			log.Printf("UDP read: %v", err)
			continue
		}
		if n < 1 {
			continue
		}
		id := findOrCreateClient(src)
		if id >= 0 {
			handlePacket(conn, id, buf[:n])
		}
	}
}

func findOrCreateClient(src *net.UDPAddr) int {
	srcStr := src.String()

	shmMu.Lock()
	defer shmMu.Unlock()

	noTamas := int(shmNoTamas())
	for i := 0; i < noTamas; i++ {
		if shmLastSeq(i) != unusedSeq && clients[i] != nil &&
			clients[i].addr.String() == srcStr {
			return i
		}
	}

	// New client — find a free slot.
	slot := -1
	for i := 0; i < noTamas; i++ {
		if shmLastSeq(i) == unusedSeq {
			slot = i
			break
		}
	}
	if slot == -1 {
		if noTamas >= maxTamas {
			log.Printf("max tamas reached, dropping %s", src)
			return -1
		}
		slot = noTamas
		shmSetNoTamas(uint32(noTamas + 1))
	}

	clients[slot] = &client{addr: src, connectedTo: -1}
	shmSetLastSeq(slot, 0)
	log.Printf("new tama id=%d from %s", slot, src)
	return slot
}

func handlePacket(conn *net.UDPConn, id int, pkt []byte) {
	pktType := pkt[0]

	if pktType == pktBye {
		shmMu.Lock()
		shmSetLastSeq(id, unusedSeq)
		clients[id] = nil
		shmMu.Unlock()
		log.Printf("tama %d says bye", id)
		return
	}

	shmMu.Lock()

	var forwardTo *net.UDPAddr
	var forwardPkt []byte

	switch pktType {
	case pktImage:
		if len(pkt) < 1+dispSize+2 {
			shmMu.Unlock()
			return
		}
		off := tamaOffset(id)
		// Copy pixel data then icons bytes as-is (matching C server behaviour).
		copy(shm[off+4:off+4+dispSize], pkt[1:1+dispSize])
		copy(shm[off+4+dispSize:off+4+dispSize+2], pkt[1+dispSize:1+dispSize+2])

	case pktIRStart:
		noTamas := int(shmNoTamas())
		if noTamas >= 2 {
			currSeq := shmCurrSeq()
			for attempts := 100; attempts > 0; attempts-- {
				x := rand.Intn(noTamas)
				if x != id && shmLastSeq(x) != unusedSeq &&
					currSeq-shmLastSeq(x) <= 100 {
					clients[id].connectedTo = x
					clients[x].connectedTo = id
					forwardTo = clients[x].addr
					forwardPkt = pkt
					log.Printf("IRSTART type %d from %d to %d", int(pkt[1]), id, x)
					break
				}
			}
		}

	case pktIRStartAck:
		if peer := clients[id].connectedTo; peer >= 0 && clients[peer] != nil {
			forwardTo = clients[peer].addr
			forwardPkt = pkt
			log.Printf("IRSTARTACK type %d from %d to %d", int(pkt[1]), id, peer)
		}

	case pktIRData:
		if peer := clients[id].connectedTo; peer >= 0 && clients[peer] != nil {
			forwardTo = clients[peer].addr
			forwardPkt = pkt
		}
	}

	seq := shmCurrSeq() + 1
	shmSetLastSeq(id, seq)
	shmSetCurrSeq(seq)
	shmMu.Unlock()

	if forwardTo != nil {
		if _, err := conn.WriteToUDP(forwardPkt, forwardTo); err != nil {
			log.Printf("UDP forward: %v", err)
		}
	}
}

// --- HTTP handlers ---

type tamaEntry struct {
	ID     int    `json:"id"`
	Pixels string `json:"pixels"`
	Icons  uint16 `json:"icons"`
}

type apiResponse struct {
	NoTama  uint32      `json:"notama"`
	LastSeq uint32      `json:"lastseq"`
	Tama    []tamaEntry `json:"tama"`
}

func handleGetTama(w http.ResponseWriter, r *http.Request) {
	var lastSeq uint32
	if v := r.URL.Query().Get("lastseq"); v != "" {
		if n, err := strconv.ParseUint(v, 10, 32); err == nil {
			lastSeq = uint32(n)
		}
	}

	shmMu.RLock()
	currSeq := binary.LittleEndian.Uint32(shm[0:4])
	noTamas := binary.LittleEndian.Uint32(shm[4:8])

	resp := apiResponse{
		NoTama:  noTamas,
		LastSeq: currSeq,
		Tama:    []tamaEntry{},
	}

	if currSeq > lastSeq {
		for i := uint32(0); i < noTamas && i < maxTamas; i++ {
			off := int(8 + i*tamaStride)
			tamaLastSeq := binary.LittleEndian.Uint32(shm[off : off+4])
			if tamaLastSeq != unusedSeq && tamaLastSeq > lastSeq {
				px := make([]byte, dispSize)
				for x := 0; x < dispSize; x++ {
					px[x] = shm[off+4+x] + 65
				}
				icons := binary.BigEndian.Uint16(shm[off+4+dispSize : off+4+dispSize+2])
				resp.Tama = append(resp.Tama, tamaEntry{
					ID:     int(i),
					Pixels: string(px),
					Icons:  icons,
				})
			}
		}
	}
	shmMu.RUnlock()

	w.Header().Set("Content-Type", "application/json")
	if err := json.NewEncoder(w).Encode(resp); err != nil {
		log.Printf("encode response: %v", err)
	}
}

func handlePressBtn(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	id, err := strconv.Atoi(r.URL.Query().Get("id"))
	if err != nil || id < 0 || id >= maxTamas {
		http.Error(w, "bad id", http.StatusBadRequest)
		return
	}
	btn, err := strconv.Atoi(r.URL.Query().Get("btn"))
	if err != nil || btn < 0 || btn > 2 {
		http.Error(w, "bad btn", http.StatusBadRequest)
		return
	}

	shmMu.RLock()
	c := clients[id]
	shmMu.RUnlock()

	if c == nil {
		http.Error(w, "tama not found", http.StatusNotFound)
		return
	}
	if _, err := udpConn.WriteToUDP([]byte{pktBtn, byte(btn)}, c.addr); err != nil {
		log.Printf("btn send to tama %d: %v", id, err)
		http.Error(w, "send failed", http.StatusInternalServerError)
		return
	}
	w.WriteHeader(http.StatusNoContent)
}

func spaHandler(staticDir string) http.Handler {
	fs := http.FileServer(http.Dir(staticDir))
	index := filepath.Join(staticDir, "index.html")
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		path := filepath.Join(staticDir, filepath.Clean("/"+r.URL.Path))
		if _, err := os.Stat(path); os.IsNotExist(err) {
			http.ServeFile(w, r, index)
			return
		}
		fs.ServeHTTP(w, r)
	})
}

func main() {
	staticDir := flag.String("static", "/var/www/html", "SvelteKit static build directory")
	listen := flag.String("listen", ":80", "listen address")
	flag.Parse()

	// Create (or attach to existing) SHM segment.
	shmID, err := unix.SysvShmGet(shmKey, shmTotal, unix.IPC_CREAT|0666)
	if err != nil {
		log.Fatalf("shmget key=%d: %v", shmKey, err)
	}
	shm, err = unix.SysvShmAttach(shmID, 0, 0)
	if err != nil {
		log.Fatalf("shmat id=%d: %v", shmID, err)
	}

	// Initialise SHM — all slots unused.
	shmSetCurrSeq(0)
	shmSetNoTamas(0)
	for i := 0; i < maxTamas; i++ {
		shmSetLastSeq(i, unusedSeq)
	}

	// Start UDP server (receives display/IR data from emulators).
	go runUDP(":7531")

	mux := http.NewServeMux()
	mux.HandleFunc("/gettama.php", handleGetTama)
	mux.HandleFunc("/presstama.php", handlePressBtn)
	mux.Handle("/", spaHandler(*staticDir))

	log.Printf("HTTP listening on %s, static=%s", *listen, *staticDir)
	log.Fatal(http.ListenAndServe(*listen, mux))
}
