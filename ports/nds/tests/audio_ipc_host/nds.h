#ifndef DOK_AUDIO_IPC_TEST_NDS_H
#define DOK_AUDIO_IPC_TEST_NDS_H
#include <stddef.h>
#include <stdint.h>
typedef uint8_t u8;
#define FIFO_USER_01 1
void DC_FlushRange(const void *, size_t);
void DC_InvalidateRange(const void *, size_t);
extern uint16_t test_fifo_control;
#define REG_IPC_FIFO_CR test_fifo_control
void *memUncached(void *);
int fifoSendDatamsg(int, int, u8 *);
int fifoSendAddress(int,void *);
int fifoCheckAddress(int);
void swiWaitForVBlank(void);
#endif
