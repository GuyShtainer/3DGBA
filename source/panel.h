// panel.h — phase 32 track T: the single-game touch PANEL (docs/phase32-voxel/SPEC-touch-panel.md).
//
// A Zallax-style bottom screen for one Gen-3 Pokémon game: a right-hand column of START-menu
// shortcuts, a 240x240 game view on the left, and a battle command panel. Our own code (legal
// condition 5 — ideas only from ZallaxDev/pokeemerald-3Ds-dualscreen).
//
// PURE C (CLAUDE.md rule 4): <stdint.h>/<stdbool.h>/<string.h> only. The game is reached through a
// read-only bus; the sequencer returns keys and at most one cursor-write REQUEST that main.c
// applies with the same writes smart touch's fmenu_select ships (touch.c). Nothing here writes.
#pragma once
#include <stdint.h>
#include <stdbool.h>

// ---- geometry (bottom screen 320x240) -------------------------------------------------------
#define PANEL_VIEW_W    240     // game view: x 0..239
#define PANEL_VIEW_H    240
#define PANEL_FRAME_Y   40      // the 240x160 GBA frame sits 1:1 at y 40..199
#define PANEL_COL_X     244     // column buttons x 244..315
#define PANEL_COL_W     72
#define PANEL_COL_HIT_X 240     // every touch at x >= 240 belongs to the column (no dead gap)

typedef enum {
	PB_MAP = 0, PB_POKEMON, PB_BAG, PB_TRAINER, PB_POKEDEX, PB_POKENAV, PB_SAVE, PB_OPTION,
	PB_COUNT
} PanelButton;

typedef enum { PFAM_EM = 0, PFAM_FRLG = 1 } PanelFamily;

typedef struct { int x, y, w, h; } PanelRect;

extern const char* const PANEL_BUTTON_LABEL[PB_COUNT];

int         panel_rows(PanelFamily fam);                       // 8 (EM) / 6 (FRLG: no MAP, no POKéNAV)
PanelButton panel_row_button(PanelFamily fam, int row);        // PB_COUNT if row out of range
bool        panel_row_rect(PanelFamily fam, int row, PanelRect* r);
int         panel_column_hit(PanelFamily fam, int sx, int sy); // row index, -1 if not the column
bool        panel_view_to_gba(int sx, int sy, int* gx, int* gy); // false outside the 240x160 frame

// ---- live START list ------------------------------------------------------------------------
typedef struct {
	uint8_t  (*rd8 )(void* ctx, uint32_t addr);
	uint16_t (*rd16)(void* ctx, uint32_t addr);
	uint32_t (*rd32)(void* ctx, uint32_t addr);
	void*    ctx;
} PanelBus;

typedef struct {
	PanelFamily fam;
	uint32_t startCount;     // EM sNumStartMenuActions / FR sNumStartMenuItems (u8)
	uint32_t startActions;   // EM sCurrentStartMenuActions / FR sStartMenuOrder (u8[9])
	uint32_t battleMons;     // gBattleMons (stride 0x58)
	uint32_t moveNames;      // ROM gMoveNames (13 B each); 0 = names unverified -> "MOVE n"
	uint32_t battleMoves;    // ROM gBattleMoves (12 B each); 0 = no type/maxPP
	uint32_t typeNames;      // ROM gTypeNames (7 B each)
} PanelAddrs;

#define PANEL_START_MAX 9
typedef struct {
	bool    valid;           // read at least once while the START menu was up
	uint8_t n;
	uint8_t act[PANEL_START_MAX];
} PanelStartList;

void panel_read_start(const PanelBus* bus, const PanelAddrs* a, PanelStartList* out);
// The game's START action id this button opens (MAP -> the POKéNAV action on EM), or -1.
int  panel_button_action(PanelFamily fam, PanelButton b, const PanelStartList* list);
int  panel_list_index(const PanelStartList* list, int action);   // -1 if absent
// Dimmed/inert when a VALID list lacks the action; an unread list enables everything.
bool panel_button_enabled(PanelFamily fam, PanelButton b, const PanelStartList* list);

// ---- the closed-loop sequencer (SPEC T2) ----------------------------------------------------
typedef enum {
	PCTX_OTHER = 0,     // title, inert, unknown: never starts, aborts a running sequence
	PCTX_FIELD,         // free-roam overworld
	PCTX_START,         // the START menu is up (and dispatching)
	PCTX_MENU,          // any other field menu / full-screen UI (party, bag, dex, card, ...)
	PCTX_POKENAV_MAIN,  // PokéNav main menu with a live cursor
	PCTX_BATTLE
} PanelCtx;

typedef struct {
	PanelCtx ctx;
	bool     fieldLock;     // a script owns the field (only consulted in PCTX_FIELD)
	int      startCursor;   // sStartMenuCursorPos while PCTX_START, else -1
	int      pnCursor;      // PokéNav main-menu cursor while PCTX_POKENAV_MAIN, else -1
	uint16_t padKeys;       // physical GBA keys held this frame (a new press cancels)
	bool     link;          // a link session is live: never start
	PanelStartList list;    // fresh while PCTX_START
} PanelSeqIn;

typedef struct {
	uint16_t keys;          // GBA key mask to inject this frame (bit 0 = A ... as GBAKEY_*)
	int      startCursor;   // >= 0: write sMenu.cursorPos + the START cursor mirror to this, then A
} PanelSeqOut;

enum { PSEQ_IDLE = 0, PSEQ_RUNNING = 1, PSEQ_DONE = 2, PSEQ_ABORTED = 3 };
enum { PABORT_NONE = 0, PABORT_CTX, PABORT_LOCKED, PABORT_TIMEOUT, PABORT_ABSENT, PABORT_USER,
       PABORT_LINK, PABORT_EXITFAIL };

typedef struct {
	int         status;     // PSEQ_*
	int         abortWhy;   // PABORT_*
	PanelButton btn;
	PanelFamily fam;
	int         step;       // internal
	int         timer;      // frames spent in this step
	int         sub;        // internal sub-counter (key pulse phase / B presses)
	uint16_t    prevPad;
} PanelSeq;

#define PANEL_WAIT_FRAMES  60     // any single wait
#define PANEL_NAV_WAIT    150     // PokéNav opens through a fade
#define PANEL_B_GAP         8     // frames between B presses when backing out
#define PANEL_B_MAX         6

void        panel_seq_start(PanelSeq* s, PanelFamily fam, PanelButton b);
void        panel_seq_cancel(PanelSeq* s);
PanelSeqOut panel_seq_step(PanelSeq* s, const PanelSeqIn* in);
bool        panel_seq_busy(const PanelSeq* s);

// ---- battle view model (SPEC T3) ------------------------------------------------------------
#define PANEL_NAME_MAX 40
typedef struct {
	bool     valid;
	uint16_t species;
	char     nick[PANEL_NAME_MAX];
	int      level, hp, maxHp;
} PanelMon;

typedef struct {
	uint16_t id;            // 0 = empty slot
	char     name[PANEL_NAME_MAX];
	int      type;          // 0..17, -1 unknown
	int      pp, maxPp;     // maxPp -1 unknown
} PanelMove;

typedef struct {
	PanelMon  self, foe;
	PanelMove mv[4];
	int       nMoves;
} PanelBattle;

// Text decode hook (gbatext_decode in the app; the host suite passes its own).
typedef int (*PanelDecodeFn)(const uint8_t* src, int n, char* out, int cap);

int  panel_max_pp(int basePp, int bonus);   // base + floor(base*bonus/5), bonus 0..3
void panel_read_battle(const PanelBus* bus, const PanelAddrs* a, PanelDecodeFn dec,
                       int selfBattler, int foeBattler, PanelBattle* out);
// 18 Gen-3 types (9 = "???"): our own palette, RGBA8 0xRRGGBBAA. Out of range -> grey.
uint32_t    panel_type_rgba(int type);
const char* panel_type_label(int type);     // our own short labels, used when typeNames is 0

// Battle cells in GBA space (the geometry smart touch's hit_action/hit_move test), so a panel
// tap can be replayed into touch_update(TOUCH_SMART) as a synthetic contact (SPEC T3).
// kind 0 = action (FIGHT 0, BAG 1, POKéMON 2, RUN 3), kind 1 = move slot 0..3.
bool panel_battle_cell(int kind, int slot, int* gx, int* gy);

// Battle panel layout on the bottom screen (SPEC T3).
bool panel_battle_action_rect(int slot, PanelRect* r);  // 0 FIGHT (wide) 1 BAG 2 POKéMON 3 RUN
bool panel_battle_move_rect(int slot, PanelRect* r);
void panel_battle_back_rect(PanelRect* r);
int  panel_battle_action_hit(int sx, int sy);           // slot or -1
int  panel_battle_move_hit(int sx, int sy);             // slot, 4 = BACK, or -1
