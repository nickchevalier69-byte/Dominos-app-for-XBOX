#ifndef PIZZA_I18N_H
#define PIZZA_I18N_H
extern int ui_lang;                 /* 0 English, 1 Spanish */
const char *tr(const char *english);   /* returns the Spanish text when ui_lang == 1 and one is known, else the same string */
#endif
