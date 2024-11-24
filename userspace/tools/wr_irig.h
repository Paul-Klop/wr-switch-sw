#ifndef __WR_IRIG_H
#define __WR_IRIG_H

#include "../../kernel/wbgen-regs/irig_slave_regs.h"

struct irig_time{
    int     year;
    int     mon;
    int     day;
    int     hour;
    int     min;
    int     sec;
    int     tos;
};

struct wr_irig{
  struct irig_slave *irig;
  struct irig_time t;
};

int irig_enable(struct wr_irig *wr_irig, int en);
int irig_enable_status(struct wr_irig *wr_irig);
int irig_read_utc(struct wr_irig *wr_irig, int64_t *t_out);

#endif
