#if defined(ARDUINO_FSP)
#include <FspTimer.h>

class UnoR4Timer: public FspTimer
{
  public:
  UnoR4Timer(timer_mode_t timer_mode = TIMER_MODE_ONE_SHOT, uint8_t timer_type = GPT_TIMER, uint8_t timer_index = 0, timer_source_div_t timer_clock_divider = TIMER_SOURCE_DIV_16) 
  : m_timer_type(timer_type)
  , m_timer_index(timer_index)
  , m_timer_mode(timer_mode)
  , m_timer_clock_divider(timer_clock_divider)
  {

  }

  void setPeriod(uint32_t counts) {
    FspTimer::set_period(counts*3);
    FspTimer::start();
  }

  void initialize() {
  }

  void attachInterrupt(GPTimerCbk_f timer_callback) {
    FspTimer::begin(m_timer_mode, m_timer_type, m_timer_index, 3000, 1, m_timer_clock_divider, timer_callback, nullptr);
    FspTimer::setup_overflow_irq(12);
	  FspTimer::open();
    FspTimer::stop();
  }

  uint8_t            m_timer_type;
  uint8_t            m_timer_index;
  timer_mode_t       m_timer_mode;
  timer_source_div_t m_timer_clock_divider;
};

extern UnoR4Timer TimerGPT0;

#endif // defined(ARDUINO_FSP)
