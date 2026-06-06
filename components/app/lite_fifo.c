#include "lite_fifo.h"





// should test the data existence by isEmpty
// before call this method
//uint16_t litefifo::popData(void)
bool fifo_PopData(liteFifo_t *fifo, user_data_t *data)
{
    if (fifo_IsEmpty(fifo))
    {
        return false;
    }

    *data = fifo->buffer[fifo->outIndex++];
    fifo->outIndex %= fifo->MAX_LEN;
    if (fifo->outIndex == fifo->inIndex)
    {
        fifo->isEmptyFlag = true;
    }
    fifo->isFullFlag = false;
    return true;
}




// should test the data existence by isEmpty
// before call this method
// just test the very front data, no index operation
bool fifo_PickData(liteFifo_t *fifo, user_data_t *data)
{
    if (fifo_IsEmpty(fifo))
    {
        return false;
    }

    *data = fifo->buffer[fifo->outIndex];
    return true;
}


// append one data
// return false if already full
//bool litefifo::pushData(uint16_t data)
bool fifo_PushData(liteFifo_t *fifo, user_data_t data, bool force)
{
    if (fifo_IsFull(fifo) && (force==false))
    {
        return false;

    }

    fifo->buffer[fifo->inIndex++] = data;
    fifo->inIndex %= fifo->MAX_LEN;
    if (fifo->inIndex == fifo->outIndex)
    {
        fifo->isFullFlag = true;
    }
    fifo->isEmptyFlag = false;
    return true;
}




bool fifo_Reset(liteFifo_t *fifo)
{
    fifo->inIndex = 0;
    fifo->outIndex = 0;
    fifo->isFullFlag = false;
    fifo->isEmptyFlag = true;
    return true;
}



bool fifo_IsFull(liteFifo_t *fifo)
{
    return fifo->isFullFlag;
}


bool fifo_IsEmpty(liteFifo_t *fifo)
{
    return fifo->isEmptyFlag;
}




void fifo_CreateLiteFifo(liteFifo_t *fifo, uint16_t maxLen, user_data_t *buf)
{
    // TODO Auto-generated constructor stub
    fifo->MAX_LEN = maxLen;
    fifo->buffer = buf;
    fifo->inIndex = 0;
    fifo->outIndex = 0;
    fifo->isEmptyFlag = true;
    fifo->isFullFlag = false;
}


void fifo_CopyData(liteFifo_t *fifo, user_data_t *buf, uint16_t len)
{
    if (len > fifo->MAX_LEN)
    {
        len = fifo->MAX_LEN;
    }

    // set the begin index for copy as: the end is just previous to inIndex
    uint16_t beginIndex = fifo->inIndex;
    for (uint16_t i = 0; i < len; ++i)
    {
        buf[i] = fifo->buffer[beginIndex];
        beginIndex = (beginIndex + 1) % fifo->MAX_LEN;
    }
}

