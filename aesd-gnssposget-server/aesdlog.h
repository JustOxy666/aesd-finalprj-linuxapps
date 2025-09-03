#ifndef AESDLOG_H
#define AESDLOG_H

extern void aesdlog_init(void);
extern void aesdlog_info(const char *message, ...);
extern void aesdlog_dbg_info(const char *message, ...);
extern void aesdlog_err(const char *message, ...);

#endif /* AESDLOG_H */