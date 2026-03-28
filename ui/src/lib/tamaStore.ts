import { writable } from 'svelte/store';

export interface TamaData {
	id: number;
	pixels: string;
	icons: number;
}

interface TamaState {
	tamas: Map<number, TamaData>;
	notama: number;
}

function createTamaStore() {
	const { subscribe, update } = writable<TamaState>({
		tamas: new Map(),
		notama: 0
	});

	let lastseq = 0;
	let running = false;
	let timeoutId: ReturnType<typeof setTimeout> | null = null;

	async function poll() {
		if (!running) return;
		try {
			const res = await fetch(`/gettama.php?lastseq=${lastseq}`);
			if (!res.ok) throw new Error(`HTTP ${res.status}`);
			const data = await res.json();

			lastseq = data.lastseq;
			update((state) => {
				const tamas = new Map(state.tamas);
				for (const t of data.tama ?? []) {
					tamas.set(t.id, t);
				}
				return { tamas, notama: data.notama };
			});

			timeoutId = setTimeout(poll, 100);
		} catch {
			timeoutId = setTimeout(poll, 500);
		}
	}

	function start() {
		if (running) return;
		running = true;
		poll();
	}

	function stop() {
		running = false;
		if (timeoutId !== null) {
			clearTimeout(timeoutId);
			timeoutId = null;
		}
	}

	return { subscribe, start, stop };
}

export const tamaStore = createTamaStore();
