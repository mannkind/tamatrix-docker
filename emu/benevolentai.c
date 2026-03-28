/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Jeroen Domburg <jeroen@spritesmods.com> wrote this file. As long as you retain
 * this notice you can do whatever you want with this stuff. If we meet some day,
 * and you think this stuff is worth it, you can buy me a beer in return.
 * ----------------------------------------------------------------------------
 */

#include "benevolentai.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include "lcdmatch.h"
#include "screens.h"
#include "udp.h"

/* -------------------------------------------------------------------------
 * Tunable constants — adjust these to change AI behaviour
 * ------------------------------------------------------------------------- */
#define CHECKINTERVAL_MS    (60*1000)   /* how often to check hunger/happy    */
#define IR_TIMEOUT_MS       (20*1000)   /* max time to wait for IR comms       */
#define CUDDLE_PROBABILITY   100000     /* 1-in-N chance per tick to cuddle    */
#define GAME_INVITE_PROB    1500000     /* 1-in-N chance per tick to invite IR game  */
#define VISIT_INVITE_PROB   2000000     /* 1-in-N chance per tick to invite IR visit */

/* -------------------------------------------------------------------------
 * Macro definitions
 * ------------------------------------------------------------------------- */
typedef struct {
	char *name;
	char *code;
} Macro;

static Macro macros[] = {
	{"feedmeal",   "s2,p2,p2,p2,w90,p3,p3"},
	{"feedsnack",  "s2,p2,p1,p2,p2,w90,p3,p3"},
	{"train",      "s6,p2,p2,w40"},
	{"medicine",   "s7,p2,w40"},
	{"loadeep",    "w10,p2,p2,w20"},
	{"updvars",    "s1,p2,w10,p1,w10"},
	{"updexit",    "p3"},
	{"toilet",     "w10,s3,p2,p2,w50"},
	{"toiletpraise","w10,s3,p2,p2,w50,s6,p2,p1,p2,w50"},
	{"lightoff",   "p3,w20"},
	{"playstb",    "s4,p2,p2,w90,p2"},
	{"playjump",   "s4,p2,p2,w90,p1,p2"},
	{"exitgame",   "p3,w90"},
	{"stbshoot",   "p2"},
	{"dojump",     "p2"},
	{"irgamecl",   "s8,p2,p2,w1,w1,p2"},
	{"irvisitcl",  "s8,p2,p2,p1,w1,p2"},
	{"irgamema",   "s8,p2,p2,w1,p2,w1,p2"},
	{"irvisitma",  "s8,p2,p2,p1,p2,w1,p2"},
	{"irgamejmp",  "p2"},
	{"irfailexit", "p3,w5,p3,w5,p3,w5,p3"},
	{"born",       "w100,p3,w20,p3"},
	{"cuddle",     "s0,p3,w100"},
	{"tst",        "s8"},
	{"", ""}
};

/* -------------------------------------------------------------------------
 * Macro engine state (low-level button sequencer — unchanged from original)
 * ------------------------------------------------------------------------- */
#define ST_IDLE    0
#define ST_NEXT    1
#define ST_ICONSEL 2
#define ST_BTNCHECK 3

static int curMacro   = -1;
static int macroPos   = 0;
static int waitTimeMs = 0;
static int mcmd, marg;
static int mstate     = 0;
static int oldIcon    = -1;
static int iconAttempts = 0;

/* -------------------------------------------------------------------------
 * Encapsulated AI state
 * ------------------------------------------------------------------------- */
#define BA_IDLE              0
#define BA_CHECKFOOD         1
#define BA_CHECKFOOD2        2
#define BA_FEED              3
#define BA_RECHECKFOOD       4
#define BA_RECHECKLESSHUNGRY  5
#define BA_RECHECKLESSHUNGRY2 6
#define BA_STB               7
#define BA_JUMP              8
#define BA_IRVISIT           9
#define BA_IRGAME            10

#define TAMAUDP_IRTP_CANCEL  0
#define TAMAUDP_IRTP_VISIT   1
#define TAMAUDP_IRTP_GAME    2

typedef struct {
	int hunger;
	int happy;
	int oldHunger;
	int oldHappy;
	int baState;
	int baTimeMs;
	int timeout;
	int irReq;
	int irMaster;
	int oldPxCnt;
} TamaState;

static TamaState ts;

//implementation re https://xkcd.com/534/
long thisAlgorithmBecomingSkynetCost = 999999999;

/* -------------------------------------------------------------------------
 * Macro engine — public interface
 * ------------------------------------------------------------------------- */
int benevolentAiMacroRun(char *name) {
	int i = 0;
	if (mstate != ST_IDLE) return 0;
	while (strcasecmp(macros[i].name, name) != 0 && macros[i].name[0] != 0) i++;
	if (macros[i].name[0] == 0) {
		printf("Macro %s not found. Available macros:\n", name);
		i = 0;
		while (macros[i].name[0] != 0) printf(" - %s\n", macros[i++].name);
		return 0;
	}
	macroPos = 0;
	curMacro = i;
	mstate   = ST_NEXT;
	return 1;
}

void benevolentAiInit() {
	memset(&ts, 0, sizeof(ts));
	ts.baTimeMs = CHECKINTERVAL_MS - 5000;
	mstate = ST_IDLE;
	benevolentAiMacroRun("loadeep");
}

/* -------------------------------------------------------------------------
 * Macro engine — internal tick (unchanged logic from original)
 * ------------------------------------------------------------------------- */
static int macroRun(Display *lcd, int mspassed) {
	static Display oldLcd;
	static int btnPressedNo;
	if (mstate == ST_IDLE) return -1;
	waitTimeMs -= mspassed;
	if (waitTimeMs > 0) return 0;
	waitTimeMs = 0;

	if (mstate == ST_NEXT) {
		if (macros[curMacro].code[macroPos] == 0) {
			mstate = ST_IDLE;
			return -1;
		} else {
			mcmd = macros[curMacro].code[macroPos++];
			marg = atoi(&macros[curMacro].code[macroPos]);
			while (macros[curMacro].code[macroPos] != ',' &&
			       macros[curMacro].code[macroPos] != 0) macroPos++;
			if (macros[curMacro].code[macroPos] == ',') macroPos++;

			if (mcmd == 'p') {
				lcdCopy(&oldLcd, lcd);
				waitTimeMs  = 400;
				mstate      = ST_BTNCHECK;
				btnPressedNo = marg - 1;
				return (1 << (marg - 1));
			} else if (mcmd == 'w') {
				waitTimeMs = marg * 100;
				return 0;
			} else if (mcmd == 's') {
				mstate      = ST_ICONSEL;
				waitTimeMs  = 0;
				oldIcon     = -1;
				iconAttempts = 0;
				return 0;
			} else {
				printf("Huh? Unknown macro cmd %c (macro %d pos %d)\n", mcmd, curMacro, macroPos);
				exit(0);
			}
		}
	} else if (mstate == ST_BTNCHECK) {
		if (!lcdSame(&oldLcd, lcd)) {
			mstate = ST_NEXT;
		} else {
			waitTimeMs = 300;
			mstate     = ST_NEXT;
			return (1 << btnPressedNo);
		}
	} else if (mstate == ST_ICONSEL) {
		iconAttempts++;
		if (iconAttempts == 15) mstate = ST_NEXT;
		if ((marg == 0 && lcd->icons == 0) || (lcd->icons & (1 << (marg - 1)))) {
			mstate = ST_NEXT;
		} else {
			waitTimeMs = 200;
			if (oldIcon == lcd->icons) {
				oldIcon = -1;
				return (1 << 2);
			} else {
				return (1 << 0);
			}
		}
	}
	return 0;
}

/* -------------------------------------------------------------------------
 * Utility: count dark (lit) pixels
 * ------------------------------------------------------------------------- */
static int getDarkPixelCnt(Display *lcd) {
	int c = 0, x, y;
	for (y = 0; y < 32; y++)
		for (x = 0; x < 48; x++)
			if (lcd->p[y][x] == 3) c++;
	return c;
}

/* -------------------------------------------------------------------------
 * Utility: read hunger/happy from the hearts screen
 * ------------------------------------------------------------------------- */
static int updateHungerHappy(Display *lcd) {
	int i;
	if (!lcdmatch(lcd, screen_hearts)) {
		printf("WtF? Not at hungry/happy screen :/ \n");
		return 0;
	}
	ts.hunger = 0;
	ts.happy  = 0;
	for (i = 0; i < 5; i++) {
		if (lcd->p[10][i * 10 + 6] == 3) ts.hunger++;
		if (lcd->p[26][i * 10 + 6] == 3) ts.happy++;
	}
	return 1;
}

/* -------------------------------------------------------------------------
 * Debug helpers
 * ------------------------------------------------------------------------- */
void benevolentAiDump() {
	char *bastates[] = {
		"idle", "checkfood", "checkfood2", "feed",
		"recheckfood", "rechecklesshungry", "rechecklesshungry2",
		"shootthebug", "jump", "irvisit", "irgame"
	};
	printf("Current macro: ");
	if (mstate == ST_IDLE) {
		printf("None.\n");
	} else {
		printf("%s, at cmd %c arg %d\n", macros[curMacro].name, mcmd, marg);
	}
	printf("Benevolent AI state: %s\n", bastates[ts.baState]);
}

void benevolentAiReqIrComm(int type) {
	ts.irReq    = type;
	ts.irMaster = 0;
}

void benevolentAiAckIrComm(int type) {
	ts.irReq    = type;
	ts.irMaster = 1;
}

int benevolentAiDbgCmd(char *cmd) {
	if (strcmp(cmd, "IRG") == 0) {
		ts.irReq    = TAMAUDP_IRTP_GAME;
		ts.irMaster = 1;
		udpSendIrstartReq(ts.irReq);
		ts.baTimeMs = 0;
	} else if (strcmp(cmd, "IRV") == 0) {
		ts.irReq    = TAMAUDP_IRTP_VISIT;
		ts.irMaster = 1;
		udpSendIrstartReq(ts.irReq);
		ts.baTimeMs = 0;
	} else {
		printf("Commands: irg, irv\n");
	}
	return 1;
}

/* -------------------------------------------------------------------------
 * Main AI tick — called once per frame (~66ms)
 * ------------------------------------------------------------------------- */
int benevolentAiRun(Display *lcd, int mspassed) {
	int i;

	/* Run the macro engine first; if it has work, don't interfere. */
	int r = macroRun(lcd, mspassed);
	if (r != -1) return r;

	/* Advance timers. */
	ts.baTimeMs += mspassed;

	/* Timeout: bail out of a stuck state back to idle. */
	if (ts.timeout != 0) {
		ts.timeout -= mspassed;
		if (ts.timeout <= 0) {
			ts.timeout  = 0;
			ts.baState  = BA_IDLE;
		}
	}

	/* ---- IDLE: priority-ordered behavior dispatch ---- */
	if (ts.baState == BA_IDLE) {

		/* 1. CRITICAL: sick */
		if (lcdmatch(lcd, screen_sick1) || lcdmatch(lcd, screen_sick2)) {
			benevolentAiMacroRun("medicine");

		/* 2. CRITICAL: newborn */
		} else if (lcdmatch(lcd, screen_born1) || lcdmatch(lcd, screen_born2)) {
			benevolentAiMacroRun("born");

		/* 3. HIGH: poop on screen */
		} else if (lcdmatch(lcd, screen_poopie1) ||
		           lcdmatch(lcd, screen_poopie2) ||
		           lcdmatch(lcd, screen_poopie3)) {
			benevolentAiMacroRun("toilet");

		/* 4. HIGH: actively pooping */
		} else if (lcdmatchMovable(lcd, screen_pooping1, -16, 2) ||
		           lcdmatchMovable(lcd, screen_pooping2, -16, 2)) {
			benevolentAiMacroRun("toiletpraise");

		/* 5. HIGH: sleeping — turn off the light */
		} else if (lcdmatchMovable(lcd, screen_sleep1, -16, 2) ||
		           lcdmatchMovable(lcd, screen_sleep2, -2, 2)) {
			benevolentAiMacroRun("lightoff");
			ts.baTimeMs = 0;

		/* 6. HIGH: screen is dark (light already off) — don't disturb */
		} else if (getDarkPixelCnt(lcd) > 1000) {
			ts.baTimeMs = 0;

		/* 7. MEDIUM: alert screen — training opportunity */
		} else if (lcdmatch(lcd, screen_alert)) {
			benevolentAiMacroRun("train");

		/* 8. MEDIUM: periodic health check, or any attention icon */
		} else if (ts.baTimeMs > CHECKINTERVAL_MS || (lcd->icons & (1 << 9))) {
			ts.baTimeMs = 0;
			ts.baState  = BA_CHECKFOOD;

		/* 10. LOW: random cuddle */
		} else if ((rand() % CUDDLE_PROBABILITY) < mspassed) {
			benevolentAiMacroRun("cuddle");

		/* 12. LOW: random IR game invite */
		} else if (ts.baTimeMs < (CHECKINTERVAL_MS - 20000) &&
		           (rand() % GAME_INVITE_PROB) < mspassed) {
			ts.irReq    = TAMAUDP_IRTP_GAME;
			ts.irMaster = 1;
			udpSendIrstartReq(ts.irReq);

		/* 13. LOW: random IR visit invite */
		} else if (ts.baTimeMs < (CHECKINTERVAL_MS - 20000) &&
		           (rand() % VISIT_INVITE_PROB) < mspassed) {
			ts.irReq    = TAMAUDP_IRTP_VISIT;
			ts.irMaster = 1;
			udpSendIrstartReq(ts.irReq);

		/* 14. LOW: handle pending IR comms (inbound request or ack) */
		} else if (ts.irReq) {
			if (!ts.irMaster) udpSendIrstartAck(ts.irReq);
			if (ts.irReq == TAMAUDP_IRTP_GAME) {
				benevolentAiMacroRun(ts.irMaster ? "irgamema" : "irgamecl");
				ts.baState = BA_IRGAME;
				ts.timeout = IR_TIMEOUT_MS;
			} else if (ts.irReq == TAMAUDP_IRTP_VISIT) {
				benevolentAiMacroRun(ts.irMaster ? "irvisitma" : "irvisitcl");
				ts.baState = BA_IRVISIT;
				ts.timeout = IR_TIMEOUT_MS;
			}
			ts.irReq = 0;

		/* 15. IDLE: pixel snapshot — triggers a display update when screen changes */
		} else {
			i = getDarkPixelCnt(lcd);
			if (i < (ts.oldPxCnt - 10) || i > (ts.oldPxCnt + 10)) {
				ts.oldPxCnt = (i + ts.oldPxCnt) / 2;
				return 8;
			}
		}

	/* ---- FOOD CHECK STATES ---- */

	/*
	 * Food check flow:
	 *   BA_CHECKFOOD  → updvars → BA_CHECKFOOD2
	 *   BA_CHECKFOOD2 → measure hunger/happy → updexit → BA_FEED
	 *   BA_FEED:
	 *     hunger < 4       → feedmeal     → BA_RECHECKFOOD
	 *     happy < 5:
	 *       hunger >= 4    → play only (STB/jump/IR) — no snack to avoid overfeeding
	 *       hunger <  4    → snack/STB/jump/IR (all options)
	 *   BA_RECHECKFOOD → updvars → BA_RECHECKLESSHUNGRY
	 *   BA_RECHECKLESSHUNGRY  → measure → updexit → BA_RECHECKLESSHUNGRY2
	 *   BA_RECHECKLESSHUNGRY2:
	 *     values changed   → BA_FEED (try again)
	 *     values unchanged → medicine → BA_FEED (maybe sick)
	 */
	} else if (ts.baState == BA_CHECKFOOD) {
		benevolentAiMacroRun("updvars");
		ts.baState = BA_CHECKFOOD2;

	} else if (ts.baState == BA_CHECKFOOD2) {
		if (updateHungerHappy(lcd)) {
			ts.baState = BA_FEED;
		} else {
			ts.baState  = BA_IDLE;
			ts.baTimeMs = 0;
		}
		benevolentAiMacroRun("updexit");

	} else if (ts.baState == BA_FEED) {
		ts.oldHunger = ts.hunger;
		ts.oldHappy  = ts.happy;

		if (ts.hunger < 4) {
			benevolentAiMacroRun("feedmeal");
			ts.baState = BA_RECHECKFOOD;
		} else if (ts.happy < 5) {
			/*
			 * Smarter activity selection:
			 * If hunger is already full (>=4), skip snack (i=0) and only play.
			 * This avoids overfeeding while still improving happiness.
			 */
			if (ts.hunger >= 4) {
				i = rand() % 4 + 1; /* 1..4: STB, jump, IR game, IR visit */
			} else {
				i = rand() % 5;     /* 0..4: snack, STB, jump, IR game, IR visit */
			}

			if (i == 0) {
				benevolentAiMacroRun("feedsnack");
				ts.baState = BA_RECHECKFOOD;
			} else if (i == 1) {
				benevolentAiMacroRun("playstb");
				ts.baState = BA_STB;
				ts.timeout = IR_TIMEOUT_MS;
			} else if (i == 2) {
				benevolentAiMacroRun("playjump");
				ts.baState = BA_JUMP;
				ts.timeout = IR_TIMEOUT_MS;
			} else if (i == 3) {
				ts.irReq    = TAMAUDP_IRTP_GAME;
				ts.irMaster = 1;
				udpSendIrstartReq(ts.irReq);
				ts.baTimeMs = 0;
				ts.baState  = BA_IDLE;
			} else {
				ts.irReq    = TAMAUDP_IRTP_VISIT;
				ts.irMaster = 1;
				udpSendIrstartReq(ts.irReq);
				ts.baTimeMs = 0;
				ts.baState  = BA_IDLE;
			}
		} else {
			ts.baState = BA_IDLE;
		}

	} else if (ts.baState == BA_RECHECKFOOD) {
		benevolentAiMacroRun("updvars");
		ts.baState = BA_RECHECKLESSHUNGRY;

	} else if (ts.baState == BA_RECHECKLESSHUNGRY) {
		if (updateHungerHappy(lcd)) {
			ts.baState = BA_RECHECKLESSHUNGRY2;
		} else {
			ts.baState  = BA_IDLE;
			ts.baTimeMs = 0;
		}
		benevolentAiMacroRun("updexit");

	} else if (ts.baState == BA_RECHECKLESSHUNGRY2) {
		if (ts.hunger != ts.oldHunger || ts.happy != ts.oldHappy) {
			ts.baState = BA_FEED;
		} else {
			benevolentAiMacroRun("medicine");
			ts.baState = BA_FEED;
		}

	/* ---- GAME STATES ---- */

	} else if (ts.baState == BA_STB) {
		if (lcdmatchMovable(lcd, screen_stb1, -25, 0) ||
		    lcdmatchMovable(lcd, screen_stb2, -25, 0) ||
		    lcdmatchMovable(lcd, screen_stb3, -25, 0) ||
		    lcdmatchMovable(lcd, screen_stb4, -25, 0)) {
			benevolentAiMacroRun("stbshoot");
			ts.timeout = 0;
		} else if (lcdmatch(lcd, screen_gameend) || lcdmatch(lcd, screen_doorsel)) {
			benevolentAiMacroRun("exitgame");
			ts.baState = BA_RECHECKFOOD;
		}

	} else if (ts.baState == BA_JUMP) {
		if (lcdmatch(lcd, screen_jump1) || lcdmatch(lcd, screen_jump2)) {
			benevolentAiMacroRun("dojump");
			ts.timeout = 0;
		} else if (lcdmatch(lcd, screen_gameend) || lcdmatch(lcd, screen_doorsel)) {
			benevolentAiMacroRun("exitgame");
			ts.baState = BA_RECHECKFOOD;
		}

	} else if (ts.baState == BA_IRVISIT) {
		if (lcdmatch(lcd, screen_irfail)) {
			benevolentAiMacroRun("irfailexit");
			ts.baState = BA_IDLE;
		}

	} else if (ts.baState == BA_IRGAME) {
		if (lcdmatch(lcd, screen_irgame1)) {
			benevolentAiMacroRun("irgamejmp");
		} else if (lcdmatch(lcd, screen_irfail)) {
			benevolentAiMacroRun("irfailexit");
			ts.baState = BA_IDLE;
		} else if (lcdmatch(lcd, screen_gameend)) {
			benevolentAiMacroRun("exitgame");
			ts.baState = BA_IDLE;
		}
	}

	return 0;
}
