#include "main.h"
#include <string.h>

UART_HandleTypeDef huart1; // Kết nối PC qua module chuyển đổi
UART_HandleTypeDef huart2; // Giao tiếp trực tiếp nhận chuỗi Node 1

uint8_t rx_byte_tmp;
char msg_buffer[128];
uint8_t buf_idx = 0;

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_Init(void);
static void MX_USART2_Init(void);

/* --- ISR Callback: Ngắt ngoài nút bấm dừng khẩn cấp chân PE0 --- */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (GPIO_Pin == GPIO_PIN_0) {
        // Thuật toán chống rung (Debounce) bằng phần cứng đọc trạng thái vật lý ổn định
        HAL_Delay(10); // Cho phép trễ chống nhiễu sườn cơ khí của nút nhấn
        if (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_0) == GPIO_PIN_RESET) {
            uint8_t stop_signal = 'S';
            // Bắn tín hiệu khẩn cấp sang Node 1 ngay lập tức qua UART2
            HAL_UART_Transmit(&huart2, &stop_signal, 1, 20);
        }
    }
}

/* --- ISR Callback: Nhận dữ liệu tuần tự từng ký tự từ Node 1 --- */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART2) {
        if (rx_byte_tmp != '\n' && buf_idx < 127) {
            msg_buffer[buf_idx++] = rx_byte_tmp;
        } else {
            msg_buffer[buf_idx++] = '\n';
            msg_buffer[buf_idx] = '\0';

            // Đẩy trực tiếp gói dữ liệu thô dạng Text lên Laptop thông qua UART1
            HAL_UART_Transmit(&huart1, (uint8_t*)msg_buffer, strlen(msg_buffer), 50);
            buf_idx = 0; // Reset chỉ số mảng chuẩn bị gói kế tiếp
        }
        HAL_UART_Receive_IT(&huart2, &rx_byte_tmp, 1); // Đăng ký lại ngắt nhận ký tự tiếp theo
    }
}

int main(void) {
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART1_Init();
    MX_USART2_Init();

    HAL_UART_Receive_IT(&huart2, &rx_byte_tmp, 1); // Kích hoạt trạng thái chờ ngắt nhận dữ liệu từ Node 1

    while (1) {
        // Vòng lặp siêu tiết kiệm điện năng.
        // Toàn bộ logic thu thập, chuyển tiếp và can thiệp khẩn cấp đều chạy bất đồng bộ trong ISR.
    }
}

static void MX_USART1_Init(void) {
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart1) != HAL_OK) Error_Handler();
}

static void MX_USART2_Init(void) {
    huart2.Instance = USART2;
    huart2.Init.BaudRate = 115200;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1;
    huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart2) != HAL_OK) Error_Handler();
}

static void MX_GPIO_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitStruct.Pin = GPIO_PIN_0;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING; // Bắt cạnh xuống khi nhấn kéo chân về GND
    GPIO_InitStruct.Pull = GPIO_PULLUP;           // Treo điện áp mặc định cao ổn định
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

    // Cấu hình mức độ ưu tiên Vector ngắt trong hệ thống NVIC
    HAL_NVIC_SetPriority(EXTI0_IRQn, 0, 0); // Đặt ưu tiên mức 0 cao nhất tránh trễ truyền tin
    HAL_NVIC_EnableIRQ(EXTI0_IRQn);
}

void SystemClock_Config(void) {
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM = 4;
    RCC_OscInitStruct.PLL.PLLN = 84;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    HAL_RCC_OscConfig(&RCC_OscInitStruct);
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK|RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2);
}

void Error_Handler(void) {
    __disable_irq();
    while (1);
}
