#include "main.h"

extern void SystemClock_Config(void);

int main(void)
{
  HAL_Init();
  SystemClock_Config();

  while (1)
  {
  }
}
