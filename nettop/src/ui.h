#ifndef UI_H
#define UI_H

/* Tabs */
typedef enum {
    TAB_NIC     = 0,
    TAB_CPU     = 1,
    TAB_IRQ     = 2,
    TAB_SOFTNET = 3,
    TAB_SOCK    = 4,
    TAB_FLOW    = 5,
    TAB_COUNT   = 6
} tab_t;

/* Initialize ncurses, colors, mouse */
void ui_init(void);

/* Tear down ncurses */
void ui_cleanup(void);

/* Main UI loop (runs on main thread until quit) */
void ui_run(void);

#endif /* UI_H */
