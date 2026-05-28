#include "encoder.h"

// Bộ đếm xung encoder (có dấu để biết chiều quay)
static volatile int32_t encoder_pulses = 0;

void Encoder_Init(void) {
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  // Bật clock cho GPIOB
  __HAL_RCC_GPIOB_CLK_ENABLE();

  // 1. Cấu hình kênh A (PB3) làm ngắt ngoài RISING + FALLING (2X counting)
  GPIO_InitStruct.Pin  = ENCODER_A_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(ENCODER_A_PORT, &GPIO_InitStruct);

  // 2. Cấu hình kênh B (PB4) làm Input đọc hướng quay
  GPIO_InitStruct.Pin  = ENCODER_B_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(ENCODER_B_PORT, &GPIO_InitStruct);

  // 3. Chỉ kích hoạt EXTI3 cho kênh A; tắt EXTI4 vì B chỉ là input
  HAL_NVIC_SetPriority(EXTI3_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(EXTI3_IRQn);
  HAL_NVIC_DisableIRQ(EXTI4_IRQn); // B không dùng ngắt

  // 4. Kích hoạt DWT Cycle Counter để đo thời gian (dùng cho Delay_us)
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;

  Encoder_Reset();
}

void Encoder_Reset(void) { encoder_pulses = 0; }

// Stub giữ nguyên chữ ký theo encoder.h
void Encoder_Pulse_Callback(void) { /* không dùng trực tiếp */ }

/*
 * Giải mã quadrature 2X dùng kênh A (ngắt) + kênh B (đọc state):
 *
 *  Chiều CW:   A và B KHÁC nhau tại thời điểm ngắt
 *              (A lên khi B=0,  hoặc A xuống khi B=1)
 *
 *  Chiều CCW:  A và B GIỐNG nhau tại thời điểm ngắt
 *              (A lên khi B=1,  hoặc A xuống khi B=0)
 *
 *  Bảng kiểm chứng (sequence CW: 00→10→11→01→00):
 *    A rises, B=0: A≠B → +1 ✓
 *    A falls, B=1: A≠B → +1 ✓
 *  Bảng kiểm chứng (sequence CCW: 00→01→11→10→00):
 *    A rises, B=1: A==B → -1 ✓
 *    A falls, B=0: A==B → -1 ✓
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
  if (GPIO_Pin == ENCODER_A_PIN) {
    // Đọc trạng thái hiện tại của cả 2 kênh
    uint8_t curr_A = (HAL_GPIO_ReadPin(ENCODER_A_PORT, ENCODER_A_PIN) == GPIO_PIN_SET) ? 1u : 0u;
    uint8_t curr_B = (HAL_GPIO_ReadPin(ENCODER_B_PORT, ENCODER_B_PIN) == GPIO_PIN_SET) ? 1u : 0u;

    // Nếu A và B khác nhau → CW; giống nhau → CCW
    if (curr_A != curr_B) {
      encoder_pulses++;
    } else {
      encoder_pulses--;
    }
  }
}

static volatile int32_t last_pulses = 0;

extern uint32_t SystemCoreClock;

float Encoder_GetRPM(float dt) {
  if (dt <= 0.0f)
    return 0.0f;

  // Đọc số xung an toàn (disable ngắt ngắn để tránh race condition)
  __disable_irq();
  int32_t pulses = encoder_pulses;
  encoder_pulses = 0;
  __enable_irq();

  last_pulses = pulses;

  // Chế độ 2X: mỗi vòng encoder tạo ra ENCODER_PPR * 2 xung (cả cạnh lên lẫn cạnh xuống của A)
  // RPM trục ra = (|pulses| * 60) / (PPR * 2 * GEAR_RATIO * dt)
  float rpm = ((float)pulses * 60.0f) / (ENCODER_PPR * 2.0f * GEAR_RATIO * dt);

  // Trả về giá trị tuyệt đối (motor 1 chiều)
  if (rpm < 0.0f) rpm = -rpm;

  return rpm;
}

int32_t Encoder_GetLastPulses(void) { return last_pulses; }

void Delay_us(uint32_t us) {
  uint32_t start = DWT->CYCCNT;
  uint32_t ticks = us * (SystemCoreClock / 1000000);
  while ((DWT->CYCCNT - start) < ticks)
    ;
}
