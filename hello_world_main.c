#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_http_client.h"


#define WIFI_SSID "note"
#define WIFI_PASS "note1234"
static EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init() {
    wifi_event_group = xEventGroupCreate();
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
    esp_wifi_start();
    esp_wifi_connect();

    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);
    printf("Conectado ao Wi-Fi %s\n", WIFI_SSID);
}


const char* api_key = "DQC8MPEPQ1BGFT66";
const char* server = "http://api.thingspeak.com/update";

void enviar_dados(float quantidade_chuva) {
    char url[256];
    snprintf(url, sizeof(url), "http://api.thingspeak.com/update?api_key=%s&field1=%.2f", api_key, quantidade_chuva);

    // Configuração do cliente HTTP
    esp_http_client_config_t config = {
        .url = url,
    };

    // Inicializa o cliente HTTP
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // Realiza a requisição
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        printf("Dados enviados com sucesso.\n");
    } else {
        printf("Erro ao enviar dados: %s\n", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}

// pinos
#define REED_PIN GPIO_NUM_4  // Modulo
#define LED_VERDE GPIO_NUM_21   // LED Verde
#define LED_AMARELO GPIO_NUM_23 // LED Amarelo
#define LED_VERMELHO GPIO_NUM_22 // LED Vermelho

#define VOLUME_POR_BASCULAMENTO 0.27

// níveis de chuva
#define NIVEL_OK 20.0      // Nível seguro
#define NIVEL_ATENCAO 20.0  // Atenção
#define NIVEL_CRITICO 40.0  // Crítico

//reset ms
#define TEMPO_RESET 20000 //1 hora

volatile int basculamentos = 0;  // Contador de basculamentos
uint32_t ultimo_basculamento = 0; // Armazena o tempo do último basculamento

// Função de tratamento de interrupção
static void IRAM_ATTR reed_switch_isr_handler(void* arg) {
    basculamentos++;
    ultimo_basculamento = xTaskGetTickCount(); // Atualiza o tempo do último basculamento
}

// Função para atualizar os LEDs de acordo com a quantidade de chuva
void atualizar_leds(float quantidade_chuva) {
    if (quantidade_chuva < NIVEL_OK) {            // Acende LED verde
        gpio_set_level(LED_VERDE, 1);
        gpio_set_level(LED_AMARELO, 0);
        gpio_set_level(LED_VERMELHO, 0);
    } else if (quantidade_chuva >= NIVEL_OK && quantidade_chuva < NIVEL_CRITICO) {// Acende LED amarelo
        gpio_set_level(LED_VERDE, 0);
        gpio_set_level(LED_AMARELO, 1);
        gpio_set_level(LED_VERMELHO, 0);
    } else {                                      // Acende LED vermelho
        gpio_set_level(LED_VERDE, 0);
        gpio_set_level(LED_AMARELO, 0);
        gpio_set_level(LED_VERMELHO, 1);
    }
}

void app_main(void) {
     // Inicializa o NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Inicia a conexão Wi-Fi
    wifi_init();

    // Configura os pinos do Reed Switch e LEDs
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE,  // Interrupção
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << REED_PIN),
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE
    };
    gpio_config(&io_conf);

    // Configura os pinos dos LEDs como saída
    gpio_set_direction(LED_VERDE, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_AMARELO, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_VERMELHO, GPIO_MODE_OUTPUT);

    // Liga o LED verde inicialmente
    gpio_set_level(LED_VERDE, 1);
    gpio_set_level(LED_AMARELO, 0);
    gpio_set_level(LED_VERMELHO, 0);

    // inicia serviço de interrupção e anexa o handler
    gpio_install_isr_service(0);
    gpio_isr_handler_add(REED_PIN, reed_switch_isr_handler, NULL);

    while (1) {

        float quantidade_chuva = basculamentos * VOLUME_POR_BASCULAMENTO;

        atualizar_leds(quantidade_chuva);

        printf("Quantidade de chuva acumulada: %.2f mm\n", quantidade_chuva);

        enviar_dados(quantidade_chuva);

        // Verifica se passou o tempo de reset
        uint32_t tempo_atual = xTaskGetTickCount();
        if (tempo_atual - ultimo_basculamento > TEMPO_RESET / portTICK_PERIOD_MS) {
            basculamentos = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
