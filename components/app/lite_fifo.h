/*
 * lite_fifo.h
 * A very simple fifo for embedded system
 * And with enough flexibity
 * Kim Jin @ jintonglei@126.com
 */

#ifndef __LITE_FIFO_H__
#define __LITE_FIFO_H__

#include <stdbool.h>
#include "stdint.h"
#include "user_data_type.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _litefifo
{

    uint16_t MAX_LEN;
    uint16_t inIndex;
    uint16_t outIndex;
    user_data_t *buffer;
    bool isEmptyFlag;
    bool isFullFlag;

} liteFifo_t;



extern bool fifo_PopData(liteFifo_t *fifo, user_data_t *data);
extern bool fifo_PushData(liteFifo_t *fifo, user_data_t data, bool force);
extern bool fifo_Reset(liteFifo_t *fifo);
extern bool fifo_IsFull(liteFifo_t *fifo);
extern bool fifo_IsEmpty(liteFifo_t *fifo);
extern void fifo_CreateLiteFifo(liteFifo_t *fifo, uint16_t maxLen, user_data_t *buf);
extern bool fifo_PickData(liteFifo_t *fifo, user_data_t *data);
extern void fifo_CopyData(liteFifo_t *fifo, user_data_t *buf, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* LITE_FIFO_H_ */
