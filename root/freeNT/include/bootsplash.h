/* bootsplash.h */
#ifndef BOOTSPLASH_H
#define BOOTSPLASH_H

void bootsplash_start(void);
void bootsplash_progress(int percent);
void bootsplash_finish(void);
void bootsplash_relayout(void);
int  bootsplash_active(void);

#endif
