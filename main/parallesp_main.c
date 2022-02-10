// ParallESP Parallel Printer Emulator

#include <stdio.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "soc/gpio_reg.h"

#define ESP_INTR_FLAG_DEFAULT 0

QueueHandle_t ioQueue;
QueueHandle_t parallelByteQueue;
gptimer_handle_t handshakeTimer;

static void IRAM_ATTR getParallelByteISR(void) {
	REG_SET_BIT(GPIO_OUT1_W1TS_REG, BIT1);  // Set Busy High
	uint32_t ioPort = REG_READ(GPIO_IN_REG); // Read GPIO Register, all 32 pins
	xQueueSendFromISR(ioQueue, &ioPort, NULL);  // Send GPIO data to register to be processed
	REG_SET_BIT(GPIO_OUT1_W1TC_REG, BIT0); // Set ACK low
	gptimer_start(handshakeTimer);
}

static void IRAM_ATTR completeHandshake(void) {
	gptimer_stop(handshakeTimer);
	gptimer_set_raw_count(handshakeTimer, 0);
	REG_SET_BIT(GPIO_OUT1_W1TS_REG, BIT0);  // Reset Busy and ACK
	REG_SET_BIT(GPIO_OUT1_W1TC_REG, BIT1);
}

static void configureIO(void) { 
	/* The ParallESP requires a minimum of 11 GPIOs.  9 inputs (Data Bits 0-7 and Strobe),
	   and 2 outputs (ACK and Busy).  Additional data lines (Auto Selection, OOP Emulation, Error State)
	   and bidirectional parallel to be implemented at a later date.  The following function initializes
	   IO for use.  GPIO Use is as follows: Strobe (4), D0-D7 (18, 19, 21, 22, 23, 25, 26, 27), ACK (32),
	   and Busy (33). */
	
	printf("Configuring interrupt:\n");
	gpio_config_t intConfig = {
		.intr_type = GPIO_INTR_NEGEDGE,
		.pin_bit_mask = BIT4,
		.mode = GPIO_MODE_INPUT,
		.pull_down_en = 0,
		.pull_up_en = 0,
	};
	gpio_config(&intConfig);
	gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
	gpio_isr_handler_add(4, getParallelByteISR, NULL);

	printf("Configuring outputs:\n");
	gpio_config_t outConfig = {
		.intr_type = GPIO_INTR_DISABLE,
		.mode = GPIO_MODE_OUTPUT,
		.pin_bit_mask = (BIT32 + BIT33),
		.pull_down_en = 0,
		.pull_up_en = 0,
	};
	gpio_config(&outConfig);

	printf("Configuring inputs:\n");
	gpio_config_t inConfig = {
		.intr_type = GPIO_INTR_DISABLE,
		.mode = GPIO_MODE_INPUT,
		.pin_bit_mask = (BIT18 + BIT19 + BIT21 + BIT22 + BIT23 + BIT25 + BIT26 + BIT27),
		.pull_down_en = 0,
		.pull_up_en = 0,
	};
	gpio_config(&inConfig);

	REG_SET_BIT(GPIO_OUT1_W1TS_REG, BIT0);
	REG_SET_BIT(GPIO_OUT1_W1TC_REG, BIT1);
}

static void configureTimer(void) {
	gptimer_config_t handshakeTimerConfig = {
		.clk_src = GPTIMER_CLK_SRC_APB,
		.direction = GPTIMER_COUNT_UP,
		.resolution_hz = 1000000,  // 1MHz Timer Resolution
	};
	gptimer_new_timer(&handshakeTimerConfig, &handshakeTimer);
	gptimer_alarm_config_t handshakeAlarm = {
		.alarm_count = 1, // Alarm after 1us
	};
	gptimer_set_alarm_action(handshakeTimer, &handshakeAlarm);
	gptimer_event_callbacks_t handshakeCallback = {
		.on_alarm = completeHandshake,
	};
	gptimer_register_event_callbacks(handshakeTimer, &handshakeCallback, NULL);
}

static void processIoToData(void) {
	uint32_t ioData = 0;

	while(1) {
		xQueueReceive(ioQueue, &ioData, portMAX_DELAY);
		char dataOut = ((ioData & 0xC0000) >> 18) + ((ioData & 0xE00000) >> 19) + ((ioData & 0xE000000) >> 20);
		xQueueSend(parallelByteQueue, &dataOut, (TickType_t) 0);
	}
}

static void outputData(void) {
	char dataToSend = 0;
	while(1) {
		xQueueReceive(parallelByteQueue, &dataToSend, portMAX_DELAY);
		printf("%c", dataToSend);
	}
}

void app_main(void) {
	printf("ParallESP Parallel Printer Emulator\n");
	printf("Initializing...\n");
	
	configureIO();
	configureTimer();

	ioQueue = xQueueCreate(1000, sizeof(uint32_t));
	parallelByteQueue = xQueueCreate(1000, sizeof(char));
	
	if ((ioQueue == NULL) || (parallelByteQueue == NULL)) {
		printf("Failed to initialize queue(s)!\n");
		printf("ESP Restarting!\n");
		esp_restart();
	}
	
	xTaskCreate(processIoToData, "Process IO Data to Chars", 4096, NULL, tskIDLE_PRIORITY, NULL);
	xTaskCreate(outputData, "Output Data over Serial", 4096, NULL, tskIDLE_PRIORITY, NULL);
}
