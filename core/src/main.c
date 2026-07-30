#include "main.h"
#include <stdio.h>
#include <string.h>

I2C_HandleTypeDef hi2c1;
I2C_HandleTypeDef hi2c2;
TIM_HandleTypeDef htim2;
UART_HandleTypeDef huart1;

#define DHT12_ADDRESS (0x5C << 1) // 0xB8
#define LCD_ADDRESS   (0x27 << 1) // 0x4E

volatile uint8_t dht12_buf[5] = {0};   // SỬA: Sửa lại cú pháp khai báo mảng tĩnh chuẩn xác
volatile float temperature = 0.0;         
volatile float humidity = 0.0;            
char uart_msg[32] = {0};               // SỬA: Khai báo kích thước mảng tĩnh tường minh tránh lỗi Incomplete Type
char lcd_msg[32] = {0};                // SỬA: Khai báo kích thước mảng tĩnh tường minh tránh lỗi biên dịch
uint8_t rx_cmd_byte;
volatile uint16_t pwm_duty = 0;           
volatile uint8_t is_motor_stopped = 0;

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_I2C2_Init(void);
static void MX_TIM2_Init(void);
static void MX_USART1_Init(void);

/* --- Thư viện LCD I2C tối giản tầng cứng --- */
void lcd_send_cmd(char cmd) {
    char data_u = (cmd & 0xF0);
    char data_l = ((cmd << 4) & 0xF0);
    // SỬA: Khởi tạo mảng có kích thước tĩnh rõ ràng chuẩn cú pháp C
    uint8_t data_t[4] = {
        (uint8_t)(data_u | 0x0C), // en=1, rs=0, backlight=1 (0x08)
        (uint8_t)(data_u | 0x08), // en=0, rs=0, backlight=1
        (uint8_t)(data_l | 0x0C), // en=1, rs=0, backlight=1
        (uint8_t)(data_l | 0x08)  // en=0, rs=0, backlight=1
    };
    HAL_I2C_Master_Transmit(&hi2c2, LCD_ADDRESS, data_t, 4, 50);
}

void lcd_send_data(char data) {
    char data_u = (data & 0xF0);
    char data_l = ((data << 4) & 0xF0);
    // SỬA: Khởi tạo mảng có kích thước tĩnh rõ ràng chuẩn cú pháp C
    uint8_t data_t[4] = {
        (uint8_t)(data_u | 0x0D), // en=1, rs=1, backlight=1
        (uint8_t)(data_u | 0x09), // en=0, rs=1, backlight=1
        (uint8_t)(data_l | 0x0D), // en=1, rs=1, backlight=1
        (uint8_t)(data_l | 0x09)  // en=0, rs=1, backlight=1
    };
    HAL_I2C_Master_Transmit(&hi2c2, LCD_ADDRESS, data_t, 4, 50);
}

void lcd_init(void) {
    HAL_Delay(50);
    lcd_send_cmd(0x30); HAL_Delay(5);
    lcd_send_cmd(0x30); HAL_Delay(1);
    lcd_send_cmd(0x32); HAL_Delay(10);
    lcd_send_cmd(0x28); HAL_Delay(1);
    lcd_send_cmd(0x0C); HAL_Delay(1);
    lcd_send_cmd(0x06); HAL_Delay(1);
    lcd_send_cmd(0x01); HAL_Delay(2);
}

void lcd_send_string(char *str) {
    while (*str) lcd_send_data(*str++);
}

void lcd_goto_xy(int row, int col) {
    uint8_t address = (row == 0) ? (0x80 + col) : (0xC0 + col);
    lcd_send_cmd(address);
}

/* --- Hàm đọc cảm biến DHT12 I2C --- */
HAL_StatusTypeDef DHT12_Read_Data(void) {
    uint8_t reg_addr = 0x00;
    if (HAL_I2C_Master_Transmit(&hi2c1, DHT12_ADDRESS, &reg_addr, 1, 50) != HAL_OK) return HAL_ERROR;
    // SỬA: Ép kiểu mảng tĩnh dht12_buf sang (uint8_t*) đúng nguyên mẫu hàm nhận dữ liệu HAL I2C
    if (HAL_I2C_Receive(&hi2c1, DHT12_ADDRESS, (uint8_t*)dht12_buf, 5, 100) != HAL_OK) return HAL_ERROR;
    
    uint8_t checksum = (dht12_buf[0] + dht12_buf[1] + dht12_buf[2] + dht12_buf[3]) & 0xFF;
    if (checksum == dht12_buf[4]) {
        humidity = dht12_buf[0] + (dht12_buf[1] * 0.1f);
        temperature = dht12_buf[2] + (dht12_buf[3] * 0.1f);
        return HAL_OK;
    }
    return HAL_ERROR;
}

/* --- ISR Callback: Nhận lệnh ngắt dừng từ Node 2 --- */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1) {
        if (rx_cmd_byte == 'S') { 
            is_motor_stopped = 1;
            __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, 0); // Ép tắt xung PWM ngay trên thanh ghi cứng
        } else if (rx_cmd_byte == 'R') { 
            is_motor_stopped = 0; 
        }
        HAL_UART_Receive_IT(&huart1, &rx_cmd_byte, 1); 
    }
}

int main(void) {
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_I2C1_Init();
    MX_I2C2_Init();
    MX_TIM2_Init();
    MX_USART1_Init();

    lcd_init();
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
    HAL_UART_Receive_IT(&huart1, &rx_cmd_byte, 1); 

    while (1) {
        if (DHT12_Read_Data() == HAL_OK) {
            lcd_goto_xy(0, 0);
            sprintf(lcd_msg, "Temp: %.1f C  ", temperature);
            lcd_send_string(lcd_msg);
            
            lcd_goto_xy(1, 0);
            sprintf(lcd_msg, "Humi: %.1f %%  ", humidity);
            lcd_send_string(lcd_msg);

            sprintf(uart_msg, "T:%.1f,H:%.1f\n", temperature, humidity);
            HAL_UART_Transmit(&huart1, (uint8_t*)uart_msg, strlen(uart_msg), 100);
        } else {
            lcd_goto_xy(0, 0);
            lcd_send_string("Sensor Error!   ");
        }

        if (!is_motor_stopped) {
            pwm_duty += 50;
            if (pwm_duty > 999) pwm_duty = 0;
            __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, pwm_duty);
        }
        HAL_Delay(1000); 
    }
}

static void MX_I2C1_Init(void) {
    hi2c1.Instance = I2C1;
    hi2c1.Init.ClockSpeed = 100000;
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2 = 0;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c1) != HAL_OK) Error_Handler();
}

static void MX_I2C2_Init(void) {
    hi2c2.Instance = I2C2;
    hi2c2.Init.ClockSpeed = 100000;
    hi2c2.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c2.Init.OwnAddress1 = 0;
    hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c2.Init.OwnAddress2 = 0;
    hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c2) != HAL_OK) Error_Handler();
}

static void MX_TIM2_Init(void) {
    TIM_OC_InitTypeDef sConfigOC = {0};
    htim2.Instance = TIM2;
    htim2.Init.Prescaler = 83;
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.Period = 999;
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    if (HAL_TIM_PWM_Init(&htim2) != HAL_OK) Error_Handler();
    
    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 0;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_1) != HAL_OK) Error_Handler();
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

static void MX_GPIO_Init(void) {
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
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
