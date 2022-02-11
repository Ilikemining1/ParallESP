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

static void IRAM_ATTR getParallelByteISR(void) {  // ISR to grab byte from GPIO
	REG_SET_BIT(GPIO_OUT1_W1TS_REG, BIT1);  // Set BUSY High
	uint32_t ioPort = REG_READ(GPIO_IN_REG); // Read GPIO Register, all 32 pins
	xQueueSendFromISR(ioQueue, &ioPort, NULL);  // Send GPIO data to register to be processed
	REG_SET_BIT(GPIO_OUT1_W1TC_REG, BIT0); // Set /ACK low
	gptimer_start(handshakeTimer); // Start timer for other half of handshake
}

static void IRAM_ATTR completeHandshake(void) {
	gptimer_stop(handshakeTimer);  // Stop and reset one-shot timer
	gptimer_set_raw_count(handshakeTimer, 0);
	REG_SET_BIT(GPIO_OUT1_W1TS_REG, BIT0);  // Reset BUSY and /ACK
	REG_SET_BIT(GPIO_OUT1_W1TC_REG, BIT1);
}

static esp_err_t configureIO(void) {
	esp_err_t ioConfigErr;
	/* The ParallESP requires a minimum of 11 GPIOs.  9 inputs (Data Bits 0-7 and Strobe),
	   and 2 outputs (/ACK and BUSY).  Additional data lines (Auto Selection, OOP Emulation, Error State)
	   and bidirectional parallel to be implemented at a later date.  The following function initializes
	   IO for use.  GPIO Use is as follows: /STROBE (4), D0-D7 (18, 19, 21, 22, 23, 25, 26, 27), /ACK (32),
	   and BUSY (33). */
	
	printf("Configuring interrupt:\n");
	gpio_config_t intConfig = {
		.intr_type = GPIO_INTR_NEGEDGE,
		.pin_bit_mask = BIT4,
		.mode = GPIO_MODE_INPUT,
		.pull_down_en = 0,
		.pull_up_en = 0,
	};
	ioConfigErr = gpio_config(&intConfig);
	if (ioConfigErr != ESP_OK) {
		return ioConfigErr;
	}
	gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
	if (ioConfigErr != ESP_OK) {
		return ioConfigErr;
	}
	gpio_isr_handler_add(4, getParallelByteISR, NULL);
	if (ioConfigErr != ESP_OK) {
		return ioConfigErr;
	}

	printf("Configuring outputs:\n");
	gpio_config_t outConfig = {
		.intr_type = GPIO_INTR_DISABLE,
		.mode = GPIO_MODE_OUTPUT,
		.pin_bit_mask = (BIT32 + BIT33),
		.pull_down_en = 0,
		.pull_up_en = 0,
	};
	gpio_config(&outConfig);
	if (ioConfigErr != ESP_OK) {
		return ioConfigErr;
	}

	printf("Configuring inputs:\n");
	gpio_config_t inConfig = {
		.intr_type = GPIO_INTR_DISABLE,
		.mode = GPIO_MODE_INPUT,
		.pin_bit_mask = (BIT18 + BIT19 + BIT21 + BIT22 + BIT23 + BIT25 + BIT26 + BIT27),
		.pull_down_en = 0,
		.pull_up_en = 0,
	};
	gpio_config(&inConfig);
	if (ioConfigErr != ESP_OK) {
		return ioConfigErr;
	}

	REG_SET_BIT(GPIO_OUT1_W1TS_REG, BIT0);  // Set initial state of flow control lines.  BUSY low, /ACK high.
	REG_SET_BIT(GPIO_OUT1_W1TC_REG, BIT1);

	return ESP_OK;
}

static void configureTimer(void) {  // Initialize One-Shot Timer Parameters
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
	gptimer_register_event_callbacks(handshakeTimer, &handshakeCallback, NULL); // Set up timer callback
}

static void processIoToData(void) {  // Task takes the uint32_t of the GPIO register and produces chars.  Pull from ioQueue, place on parallelDataQueue.
	uint32_t ioData = 0;

	while(1) {
		xQueueReceive(ioQueue, &ioData, portMAX_DELAY);  // Block task if no data is available
		char dataOut = ((ioData & 0xC0000) >> 18) + ((ioData & 0xE00000) >> 19) + ((ioData & 0xE000000) >> 20);  // Mask and shift to extract the 8 parallel bits.
		xQueueSend(parallelByteQueue, &dataOut, portMAX_DELAY);
	}
}

static void outputData(void) { // If there is data to send, task will write it to the serial port.
	char dataToSend = 0;

	while(1) {
		xQueueReceive(parallelByteQueue, &dataToSend, portMAX_DELAY);
		printf("%c", dataToSend);
	}
}

void app_main(void) {
	printf("ParallESP Parallel Printer Emulator\n"); // Do initialization stuffs.  Configure IO and Timer:
	printf("Initializing...\n");
	
	ESP_ERROR_CHECK(configureIO());
	configureTimer();

	ioQueue = xQueueCreate(1000, sizeof(uint32_t));  // Set up Queues.
	parallelByteQueue = xQueueCreate(1000, sizeof(char));
	
	if ((ioQueue == NULL) || (parallelByteQueue == NULL)) {
		printf("Failed to initialize queue(s)!\n");
		printf("ESP Restarting!\n");
		esp_restart();
	}
	
	xTaskCreate(processIoToData, "Process IO Data to Chars", 4096, NULL, tskIDLE_PRIORITY, NULL);  // Start data processing tasks.
	xTaskCreate(outputData, "Output Data over Serial", 4096, NULL, tskIDLE_PRIORITY, NULL);
}
