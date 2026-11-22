
#include <stdio.h>
#include <stdint.h>
#include "diag/Trace.h"
#include "cmsis/cmsis_device.h"

#define TIM2_CLOCK_HZ 48000000U
#define BUTTON_DEBOUNCE_MS 50U

// Global variables
volatile uint32_t Freq = 0;
volatile uint32_t Res = 0;
volatile uint32_t measured_count = 0;

volatile uint8_t button_state = 1;  // 1=PB3, 0=PB2
volatile uint8_t first_edge = 1;
volatile uint8_t measurement_ready = 0;

volatile uint32_t milliseconds = 0;
volatile uint32_t last_button_ms = 0;

// Function declarations
void SystemClock48MHz(void);
void ADC_setup(void);
void DAC_setup(void);
uint32_t start_adc(void);
void run_dac(uint32_t count);
void myGPIOB_Init(void);
void myTIM2_Init(void);
void EXTI_Init(void);
void EXTI_PA0_Init(void);
void freq(void);

// 1 ms system tick
void SysTick_Handler(void)
{
    milliseconds++;
}

// System clock: 48 MHz
void SystemClock48MHz(void)
{
    RCC->CR &= ~RCC_CR_PLLON;

    while (RCC->CR & RCC_CR_PLLRDY);

    RCC->CFGR = 0x00280000;

    RCC->CR |= RCC_CR_PLLON;

    while (!(RCC->CR & RCC_CR_PLLRDY));

    RCC->CFGR =
        (RCC->CFGR & ~RCC_CFGR_SW_Msk)
        | RCC_CFGR_SW_PLL;

    SystemCoreClockUpdate();
}

// ADC Setup: PA1
void ADC_setup(void)
{
    RCC->AHBENR |= RCC_AHBENR_GPIOAEN;

    // PA1 analog mode
    GPIOA->MODER &= ~(3U << 2);
    GPIOA->MODER |=  (3U << 2);

    GPIOA->PUPDR &= ~(3U << 2);

    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;

    // Calibrate ADC
    ADC1->CR |= ADC_CR_ADCAL;

    while (ADC1->CR & ADC_CR_ADCAL);

    // Select ADC channel 1
    ADC1->CHSELR = ADC_CHSELR_CHSEL1;

    // Long sampling time
    ADC1->SMPR = 7U;

    // Enable ADC
    ADC1->ISR = ADC_ISR_ADRDY;
    ADC1->CR |= ADC_CR_ADEN;

    while (!(ADC1->ISR & ADC_ISR_ADRDY));
}

// ADC Conversion
uint32_t start_adc(void)
{
    ADC1->ISR = ADC_ISR_EOC;

    ADC1->CR |= ADC_CR_ADSTART;

    while (!(ADC1->ISR & ADC_ISR_EOC));

    return ADC1->DR;
}

// DAC Setup: PA4
void DAC_setup(void)
{
    RCC->AHBENR |= RCC_AHBENR_GPIOAEN;

    // PA4 analog mode
    GPIOA->MODER &= ~(3U << 8);
    GPIOA->MODER |=  (3U << 8);

    GPIOA->PUPDR &= ~(3U << 8);

    RCC->APB1ENR |= RCC_APB1ENR_DACEN;

    DAC->CR |= DAC_CR_EN1;
}

// DAC Output
void run_dac(uint32_t count)
{
    DAC->DHR12R1 = count & 0xFFFU;
}

// PB2 and PB3 GPIO Setup
void myGPIOB_Init(void)
{
    RCC->AHBENR |= RCC_AHBENR_GPIOBEN;

    // PB2 and PB3 as digital input
    GPIOB->MODER &= ~0xF0U;

    // No pull-up or pull-down
    GPIOB->PUPDR &= ~0xF0U;
}

// TIM2 Setup
void myTIM2_Init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;

    // Stop timer, normal up-counting mode
    TIM2->CR1 = 0;

    // No prescaler
    TIM2->PSC = 0;

    // 32-bit maximum
    TIM2->ARR = 0xFFFFFFFFU;

    // Load prescaler
    TIM2->EGR = TIM_EGR_UG;

    TIM2->SR = 0;
    TIM2->DIER = 0;
    TIM2->CNT = 0;
}

// EXTI2 and EXTI3 Setup
void EXTI_Init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGCOMPEN;

    // Map EXTI2 to PB2 and EXTI3 to PB3
    SYSCFG->EXTICR[0] &= ~0x0000FF00U;
    SYSCFG->EXTICR[0] |=  0x00001100U;

    // Rising edge trigger
    EXTI->RTSR |= 0xCU;
    EXTI->FTSR &= ~0xCU;

    // Clear pending flags
    EXTI->PR = 0xCU;

    // Enable interrupts for PB2 and PB3
    EXTI->IMR |= 0xCU;

    NVIC_SetPriority(EXTI2_3_IRQn, 1);
    NVIC_EnableIRQ(EXTI2_3_IRQn);
}

// USER Button Setup: PA0
void EXTI_PA0_Init(void)
{
    RCC->AHBENR |= RCC_AHBENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGCOMPEN;

    // PA0 input
    GPIOA->MODER &= ~(3U << 0);

    // No internal pull-up/pull-down
    GPIOA->PUPDR &= ~(3U << 0);

    // Map EXTI0 to PA0
    SYSCFG->EXTICR[0] &= ~(0xFU << 0);

    // Falling edge (button release on
    // an active-high USER button)
    EXTI->FTSR |= (1U << 0);
    EXTI->RTSR &= ~(1U << 0);

    // Clear pending flag
    EXTI->PR = (1U << 0);

    // Enable EXTI0
    EXTI->IMR |= (1U << 0);

    NVIC_SetPriority(EXTI0_1_IRQn, 0);
    NVIC_EnableIRQ(EXTI0_1_IRQn);
}

// Frequency Measurement
void freq(void)
{
    if (first_edge)
    {
        // First rising edge
        TIM2->CR1 &= ~TIM_CR1_CEN;
        TIM2->CNT = 0;

        TIM2->CR1 |= TIM_CR1_CEN;

        first_edge = 0;
    }
    else
    {
        // Second rising edge
        TIM2->CR1 &= ~TIM_CR1_CEN;

        uint32_t count = TIM2->CNT;

        if (count != 0)
        {
            measured_count = count;
            Freq = TIM2_CLOCK_HZ / count;
            measurement_ready = 1;
        }

        first_edge = 1;
    }
}

// PB2/PB3 Interrupt Handler
void EXTI2_3_IRQHandler(void)
{
    uint32_t pending = EXTI->PR & 0xCU;

    // Clear both pending flags
    EXTI->PR = pending;

    if (button_state == 0)
    {
        // PB2 selected
        if (pending & EXTI_PR_PR2)
        {
            freq();
        }
    }
    else
    {
        // PB3 selected
        if (pending & EXTI_PR_PR3)
        {
            freq();
        }
    }
}

// USER Button Interrupt Handler
void EXTI0_1_IRQHandler(void)
{
    if (EXTI->PR & EXTI_PR_PR0)
    {
        // Clear pending flag
        EXTI->PR = EXTI_PR_PR0;

        uint32_t now = milliseconds;

        // Debounce button
        if ((uint32_t)(now - last_button_ms)
            >= BUTTON_DEBOUNCE_MS)
        {
            last_button_ms = now;

            // Toggle PB2 / PB3
            button_state ^= 1U;

            // Stop timer
            TIM2->CR1 &= ~TIM_CR1_CEN;

            // Reset timer
            TIM2->CNT = 0;

            // Reset frequency measurement
            first_edge = 1;
            measurement_ready = 0;
            measured_count = 0;
            Freq = 0;

            // Clear old frequency input flags
            EXTI->PR = 0xCU;
        }
    }
}

// Main Function
int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    SystemClock48MHz();

    // 1 ms SysTick
    SysTick_Config(SystemCoreClock / 1000U);

    // Allow the first button event immediately
    last_button_ms = 0U - BUTTON_DEBOUNCE_MS;

    ADC_setup();
    DAC_setup();

    myGPIOB_Init();
    myTIM2_Init();

    EXTI_Init();
    EXTI_PA0_Init();

    uint32_t last_print = 0;

    trace_printf("System Started\n");
    trace_printf("Initial Input: PB3\n");

    while (1)
    {
        // Read potentiometer
        Res = start_adc();

        // Output voltage through DAC
        run_dac(Res);

        // Print every 250 ms
        if ((uint32_t)(milliseconds - last_print) >= 250U)
        {
            last_print = milliseconds;

            // Copy shared values safely
            NVIC_DisableIRQ(EXTI0_1_IRQn);
            NVIC_DisableIRQ(EXTI2_3_IRQn);

            uint8_t selected = button_state;
            uint8_t ready = measurement_ready;
            uint32_t count = measured_count;
            uint32_t frequency = Freq;
            uint32_t adc = Res;

            NVIC_EnableIRQ(EXTI2_3_IRQn);
            NVIC_EnableIRQ(EXTI0_1_IRQn);

            if (ready)
            {
                trace_printf(
                    "PB%u  Freq=%lu Hz Resistance=%lu ohms\n",
                    selected ? 3U : 2U,
                    (unsigned long)count,
                    (unsigned long)frequency,
                    (unsigned long)adc
                );
            }
            else
            {
                trace_printf(
                    "PB%u Waiting for signal... ADC=%lu\n",
                    selected ? 3U : 2U,
                    (unsigned long)adc
                );
            }
        }
    }
}
