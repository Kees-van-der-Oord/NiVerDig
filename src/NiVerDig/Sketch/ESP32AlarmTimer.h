#include "esp_timer.h"
#if defined(ARDUINO_ARCH_ESP32)

class ESP32Timer
{
  public:
  ESP32Timer(
    int index = 0,           // the ESP32 has 4 timers (0 to 3)
    uint32_t divider = 80,   // the timer clock is 80 MHz, a divider of 80 gives 1 us granularity
    bool repeat = true)      // set repeat to false for single-shot
  {
    m_timer = timerBegin(index, divider, repeat);
  }

  ~ESP32Timer()
  {
    timerDetachInterrupt(m_timer);
    timerStop(m_timer);
    timerEnd(m_timer);
  }

  void setPeriod(
    uint32_t counts  // with a clock divider of 80, this is microseconds
    ) 
  {
    timerAlarmWrite(m_timer, counts, true);
    timerStart(m_timer); // the Timer0 library starts the counter when setting the period: for compatibility we do the same here
  }

  void initialize() {
  }

#undef attachInterrupt

  void attachInterrupt(void (*callback)(void)) {
    timerAttachInterrupt(m_timer, callback, true);
  }

  void stop()
  {
    timerStop(m_timer);
  }

  hw_timer_t* m_timer;
};

#endif // defined(ARDUINO_ARCH_ESP32)
