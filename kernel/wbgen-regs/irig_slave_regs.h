#ifndef __CHEBY__IRIG_SLAVE__H__
#define __CHEBY__IRIG_SLAVE__H__
#define IRIG_SLAVE_SIZE 20 /* 0x14 */

/* Control Register */
#define IRIG_SLAVE_CR 0x0UL
#define IRIG_SLAVE_CR_ENABLE 0x1UL

/* Time of day hr:min:sec */
#define IRIG_SLAVE_TOD 0x4UL
#define IRIG_SLAVE_TOD_SECONDS_MASK 0xffUL
#define IRIG_SLAVE_TOD_SECONDS_SHIFT 0
#define IRIG_SLAVE_TOD_MINUTES_MASK 0x1ff00UL
#define IRIG_SLAVE_TOD_MINUTES_SHIFT 8
#define IRIG_SLAVE_TOD_HOURS_MASK 0x1ff00000UL
#define IRIG_SLAVE_TOD_HOURS_SHIFT 20
#define IRIG_SLAVE_TOD_VALID 0x80000000UL

/* Date, Years / Days */
#define IRIG_SLAVE_DATE 0x8UL
#define IRIG_SLAVE_DATE_DAYS_MASK 0x7ffUL
#define IRIG_SLAVE_DATE_DAYS_SHIFT 0
#define IRIG_SLAVE_DATE_YEARS_MASK 0x1ff0000UL
#define IRIG_SLAVE_DATE_YEARS_SHIFT 16
#define IRIG_SLAVE_DATE_VALID 0x80000000UL

/* Control functions 0 & 1 */
#define IRIG_SLAVE_CTRL 0xcUL
#define IRIG_SLAVE_CTRL_FCN0_MASK 0x1ffUL
#define IRIG_SLAVE_CTRL_FCN0_SHIFT 0
#define IRIG_SLAVE_CTRL_FCN1_MASK 0x1ff0000UL
#define IRIG_SLAVE_CTRL_FCN1_SHIFT 16
#define IRIG_SLAVE_CTRL_VALID 0x80000000UL

/* Straight Binary Seconds */
#define IRIG_SLAVE_SBS 0x10UL
#define IRIG_SLAVE_SBS_SBS_MASK 0x3ffffUL
#define IRIG_SLAVE_SBS_SBS_SHIFT 0
#define IRIG_SLAVE_SBS_VALID 0x80000000UL

#ifndef __ASSEMBLER__
struct irig_slave {
  /* [0x0]: REG (rw) Control Register */
  uint32_t CR;

  /* [0x4]: REG (ro) Time of day hr:min:sec */
  uint32_t TOD;

  /* [0x8]: REG (ro) Date, Years / Days */
  uint32_t DATE;

  /* [0xc]: REG (ro) Control functions 0 & 1 */
  uint32_t CTRL;

  /* [0x10]: REG (ro) Straight Binary Seconds */
  uint32_t SBS;
};
#endif /* !__ASSEMBLER__*/

#endif /* __CHEBY__IRIG_SLAVE__H__ */
