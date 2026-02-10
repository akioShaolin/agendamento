#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ModbusRTU.h>
#include "RTClib.h"
#include <Ticker.h>
#include <WebSocketsServer.h>
#include <Wire.h>

// =============================================================================
// ==================== 1. PINAGEM DO HARDWARE (ESP07) =========================
// =============================================================================

/*

  Pinos da Serial (HardwareSerial) para comunicação RS485 TX (GPIO1) e RX (GPIO3) são os pinos padrão da Serial0 do ESP8266. O ESP8266 usa a Serial0 para upload/debug, mas pode ser reconfigurada para uso com RS485 (Não é necessário declarar os pinos, pois o ponteiro &Serial já os possui na configuração)
  Este dispositivo tem a opção de ser Full-Duplex, porém na aplicação desejada, será feito o uso de Half-Duplex, com apenas 2 fios, 1 canal de comunicação para receber e enviar os sinais

*/

// -----------------------------------------------------------------------------
// ------------------------ Configurações de Pinos -----------------------------
// -----------------------------------------------------------------------------

#define LED_PIN LED_BUILTIN //  GPIO2 - Led Built in
#define DE_RE 12 // Pino ligado ao SN75176B (RS485 transceiver)
#define BTN_PIN 0 // Botão da placa com pull up
#define HALF_OR_FULL_PIN 13 // Half-Duplex:LOW  Full-Duplex-HIGH

// =============================================================================
// =================== 2. CONFIGURAÇÕES DE COMUNICAÇÕES ========================
// =============================================================================

/*

  Esta aplicação utiliza o Modbus RTU como a comunicação principal. O dispositivo é instalado em um inversor Weg SIW500H ou SIW500G, onde fará a configuração e leitura de dados.
  O Modbus utiliza uma máquina de estados para 
  Para verificar detalhes, logs e status, utiliza-se também o WiFi para se comunicar com o usuário e definir configurações, como o ID do escravo e a potência do inversor hospedeiro.
  O acesso é feito via página web, utilizando WebServer e WebSockets para a comunicação com o usuário, seja via celular ou via computador. 

*/

// -----------------------------------------------------------------------------
// ------------------------ Configurações do Modbus ----------------------------
// -----------------------------------------------------------------------------

// ID do dispositivo escravo Modbus e baud rate
#define RS485_BAUD 9600
uint16_t SLAVE_ID = 1; //Definição Default, mas que pode ser alterada na página Web ou via EEPROM

// ---------------- Configuração dos Registradores pertinentes -----------------
#define HR_ACT_PWR_OUT      0x7D50 // Holding Register Active Power Output. Scale: 1000 [kW] (0~Pmax) 2 words
#define HR_ACT_PWR_LIM_VL   0x9CB8 // Holding Register Active Power Limit Value. Scale: 10 [kW] (0~Pmax) 1 word 9CBE
#define HR_SYS_TIME         0x9C40 // Holding Register System Time Epoch seconds local time. Scale: 1 [s] (2000-1-1 00:00:00~2068-12-31 23:59:59) 2 words

// buffer do system time do Modbus
uint16_t sys_time_regs[2] = {0, 0};
// variável da potência ativa
uint16_t power_reg;

uint32_t rtuTimer = 0;
const uint16_t MODBUS_TIMEOUT = 300;

// Status do Modbus
enum class RTUState : uint8_t {
  RTU_IDLE,
  POWER_READING,
  PROGRAMS_CHECK,
  RTC_ADJUSTING,
};

// -----------------------------------------------------------------------------
// ------------------- Configurações de WiFi AP e Client -----------------------
// -----------------------------------------------------------------------------

const char* apSsid = "InjecaoProg";
const char* apPass = "1234567890";


struct WifiCfg {
  uint32_t magic;
  char ssid[33];
  char pass[65];
  uint32_t crc;
};

static const uint32_t CFG_MAGIC = 0xC0FFEE01;
static const int EEPROM_SIZE = 256;

// -----------------------------------------------------------------------------
// ------------------------------ Página HTML ----------------------------------
// -----------------------------------------------------------------------------

const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!doctype html><html><body>
<h2>Config WiFi (NTP)</h2>
<form method="POST" action="/save">
SSID: <input name="ssid" maxlength="32"><br>
Senha: <input name="pass" type="password" maxlength="64"><br>
<button type="submit">Salvar</button>
</form>
</body></html>
)rawliteral";

// -----------------------------------------------------------------------------
// ---------------------- Configurações do Web Sockets -------------------------
// -----------------------------------------------------------------------------

uint8_t wsClient = 255;

// =============================================================================
// ======================= 3. VARIÁVEIS DE ESTADO ==============================
// =============================================================================

/*

  Faz-se o uso de timers internos do SDK, pela biblioteca Ticker de Ivan Grokhotkov (específica para esp8266), que é capaz de fazer funções não-bloqueantes. Funciona como uma espécie de thread

*/

// -----------------------------------------------------------------------------
// ------------------------ Configurações do Ticker ----------------------------
// -----------------------------------------------------------------------------

// Flags vinculadas ao Ticker para verificação de programas, leitura de potência e atualização do RTC
volatile bool tickFlag = false;
volatile bool updRTC = false;

// =============================================================================
// ====================== 4. AJUSTE PERIÓDICO DO RTC ===========================
// =============================================================================

/*

  O ajuste do RTC utiliza uma máquina de estados. Primeiro faz uma consulta ao servidor ntp, caso não der certo, faz a consulta no relógio do inversor. Caso nenhuma desses ajustes der certo, a única confiabilidade é o próprio RTC DS1307Z, que pode atrasar de 1 a 6 minutos em um mês.
  Aqui é possível fazer os ajustes em Config, como o tempo que ocorrerá cada ajuste (600.0 s por default), timeout (800ms por default) e tolerância (2s por default). As constantes UNIX são de acordo com o range do inversor, que aceita um limite de datas específicas.

*/

// -----------------------------------------------------------------------------
// -------------------------- Configurações do RTC -----------------------------
// -----------------------------------------------------------------------------

// Status do RTC. 
enum class RtcAdjState : uint8_t {
  IDLE,
  REQUEST_NTP,
  WAIT_NTP,
  REQUEST_MODBUS,
  WAIT_MODBUS,
  SET_RTC,
  DONE,
  ERROR
};

struct RtcAdjustService {
  // Config
  const float period = 600.0; // 600 segundos entre os ajustes (10 minutos)
  uint32_t ntpTimeoutMs = 3000; // NTP costuma precisar de mais tempo do que o Modbus
  uint8_t ntpMaxRetries = 1; // 1 tentativa ok
  const uint32_t stepTimeoutMs = 800; // timeout por etapa (ajuste conforme sua rede)
  const uint32_t verifyToleranceSec = 2; // Tolerância de 2 segundos
  // Unix range aceito pelo inversor: 2000-01-01 .. 2068-12-31 23:59:59
  const uint32_t UNIX_MIN_2000 = 946684800UL;
  const uint32_t UNIX_MAX_2068 = 3124223999UL;

  // Interno
  uint32_t stepStartMs = 0;
  bool pending = false;
  RtcAdjState state = RtcAdjState::IDLE;

  // Resultados
  bool modbusOk = false;
  bool modbusDone = false;
  bool dataOk = false;
  bool rtcOk = false;

  bool ntpOk = false;
  bool ntpDone = false;
  uint8_t ntpRetries = 0;

  // Dados obtidos do inversor
  uint32_t invTime = 0;
  uint32_t ntpTime = 0;
};

// =============================================================================
// ======== 5. PROGRAMAÇÃO E CHECAGEM DE HORÁRIOS E POTÊNCIA MÁXIMA ============
// =============================================================================

/*

  A programação de horários e potência máxima é o objetivo principal deste projeto. Devido a essa demanda de controle por mês, horário e dia da semana que este projeto foi criado.
  Aqui se programa o horário de início e fim e a potência máxima que o inversor pode injetar na rede.
  Futuramente poderá ser feito uma interface web onde é possível criar, remover e editar os programas e assim, serem salvos na EEPROM

*/

// -----------------------------------------------------------------------------
// ----------------------- Configurações de Programas --------------------------
// -----------------------------------------------------------------------------

enum class ProgramState : uint8_t {
  IDLE,
  EVALUATE_PROGRAMS,
  REQUEST_MODBUS,
  WAIT_MODBUS,
  DONE,
  ERROR
};

enum class ProgramOp : uint8_t {
  READ_LIMIT,   // lê HR_ACT_PWR_LIM_VL
  WRITE_LIMIT,  // escreve HR_ACT_PWR_LIM_VL
  VERIFY_READ   // relê HR_ACT_PWR_LIM_VL para confirmar 
};

uint16_t POW_INV = 75000;
const int NUM_PROGRAMS = 106;

struct Program {
  uint8_t id; // ID do programa
  bool enabled; // 1 - ativo, 0 - inativo
  uint8_t dayOfWeek; // Dia da semana (0 - Domingo, [...] , 6 - Sábado, 10 - Dias úteis, 11 - Fim de Semana)
  uint8_t month; // Mês (1 - Janeiro, 12 - Dezembro, 0 - Qualquer um)
  uint8_t startHour; // Hora do inicio evento
  uint8_t startMinute; // Minuto do inicio evento
  uint8_t endHour; // Hora do fim evento
  uint8_t endMinute; // Minuto do fim evento
  uint32_t power; // Potência (em W)
};

const Program defaultProgram PROGMEM = {0x00,  true,  0,  0,  0,  0,  0,  0}; // Caso não for definido um programa, a potência de exportação será zerada

struct ProgramService{

  // Config
  const uint32_t stepTimeoutMs = 800; // timeout por etapa (ajuste conforme sua rede)
  uint8_t maxRetries = 2;

  // Interno
  uint32_t stepStartMs = 0;
  uint8_t retries = 0;
  bool pending = false;
  uint8_t lastMinute = 0;
  ProgramState state = ProgramState::IDLE;
  ProgramOp op = ProgramOp::READ_LIMIT;
 
 // Resultados
  bool modbusOk = false;
  bool modbusDone = false;
  
  // Dados
  Program currentProgram = defaultProgram;
  uint16_t pwr_lim_reg = 0;
  uint16_t desired_reg = 0;
};

/****** DIA DA SEMANA ********
    0 - Domingo
    1 - Segunda-feira
    2 - Terça-feira
    3 - Quarta-feira
    4 - Quinta-feira
    5 - Sexta-feira
    6 - Sábado
*****************************/

// --- Programação dos eventos ---
// ID do programa
// Dia da Semana
// Hora
// Minuto
// Potência
// Ativado

const Program programs[NUM_PROGRAMS] PROGMEM = {
  // Janeiro
  {0x01, true,  10,  1,  0,  0, 10,  0,  75000},
  {0x02, true,  10,  1, 10,  0, 15,  0,      0},
  {0x03, true,  10,  1, 15,  0, 23, 59,  75000},
  {0x04, true,   6,  1,  0,  0, 11,  0,  75000},
  {0x05, true,   6,  1, 11,  0, 15,  0,      0},
  {0x06, true,   6,  1, 15,  0, 23, 59,  75000},
  {0x07, true,   0,  1,  0,  0, 10,  0,  75000},
  {0x08, true,   0,  1, 10,  0, 15,  0,      0},
  {0x09, true,   0,  1, 15,  0, 23, 59,  75000},
  // Fevereiro
  {0x0A, true,  10,  2,  0,  0, 23, 59,  75000},
  {0x0B, true,   6,  2,  0,  0, 11,  0,  75000},
  {0x0C, true,   6,  2, 11,  0, 16,  0,      0},
  {0x0D, true,   6,  2, 16,  0, 23, 59,  75000},
  {0x0E, true,   0,  2,  0,  0, 10,  0,  75000},
  {0x0F, true,   0,  2, 10,  0, 16,  0,      0},
  {0x10, true,   0,  2, 16,  0, 23, 59,  75000},
  // Março
  {0x11, true,  10,  3,  0,  0, 11,  0,  75000},
  {0x12, true,  10,  3, 11,  0, 14,  0,      0},
  {0x13, true,  10,  3, 14,  0, 23, 59,  75000},
  {0x14, true,   6,  3,  0,  0, 11,  0,  75000},
  {0x15, true,   6,  3, 11,  0, 15,  0,      0},
  {0x16, true,   6,  3, 15,  0, 23, 59,  75000},
  {0x17, true,   0,  3,  0,  0, 10,  0,  75000},
  {0x18, true,   0,  3, 10,  0, 14,  0,      0},
  {0x19, true,   0,  3, 14,  0, 15,  0,  34000},
  {0x1A, true,   0,  3, 15,  0, 23, 59,  75000},
  // Abril
  {0x1B, true,  10,  4,  0,  0, 10,  0,  75000},
  {0x1C, true,  10,  4, 10,  0, 14,  0,      0},
  {0x1D, true,  10,  4, 14,  0, 23, 59,  75000},
  {0x1E, true,   6,  4,  0,  0, 11,  0,  75000},
  {0x1F, true,   6,  4, 11,  0, 15,  0,      0},
  {0x20, true,   6,  4, 15,  0, 23, 59,  75000},
  {0x21, true,   0,  4,  0,  0, 11,  0,  75000},
  {0x22, true,   0,  4, 11,  0, 15,  0,      0},
  {0x23, true,   0,  4, 15,  0, 23, 59,  75000},
  // Maio
  {0x24, true,  10,  5,  0,  0, 10,  0,  75000},
  {0x25, true,  10,  5, 10,  0, 15,  0,      0},
  {0x26, true,  10,  5, 15,  0, 16,  0,  58000},
  {0x27, true,  10,  5, 16,  0, 23, 59,  75000},
  {0x28, true,   6,  5,  0,  0, 10,  0,  75000},
  {0x29, true,   6,  5, 10,  0, 16,  0,      0},
  {0x2A, true,   6,  5, 16,  0, 23, 59,  75000},
  {0x2B, true,   0,  5,  0,  0, 10,  0,  75000},
  {0x2C, true,   0,  5, 10,  0, 16,  0,      0},
  {0x2D, true,   0,  5, 16,  0, 23, 59,  75000},
  // Junho
  {0x2E, true,  10,  6,  0,  0, 11,  0,  75000},
  {0x2F, true,  10,  6, 11,  0, 16,  0,      0},
  {0x30, true,  10,  6, 16,  0, 23, 59,  75000},
  {0x31, true,   6,  6,  0,  0, 11,  0,  75000},
  {0x32, true,   6,  6, 11,  0, 16,  0,      0},
  {0x33, true,   6,  6, 16,  0, 23, 59,  75000},
  {0x34, true,   0,  6,  0,  0, 11,  0,  75000},
  {0x35, true,   0,  6, 11,  0, 16,  0,      0},
  {0x36, true,   0,  6, 16,  0, 23, 59,  75000},
  // Julho
  {0x37, true,  10,  7,  0,  0, 11,  0,  75000},
  {0x38, true,  10,  7, 11,  0, 16,  0,      0},
  {0x39, true,  10,  7, 16,  0, 23, 59,  75000},
  {0x3A, true,   6,  7,  0,  0, 11,  0,  75000},
  {0x3B, true,   6,  7, 11,  0, 16,  0,      0},
  {0x3C, true,   6,  7, 16,  0, 23, 59,  75000},
  {0x3D, true,   0,  7,  0,  0, 10,  0,  75000},
  {0x3E, true,   0,  7, 10,  0, 16,  0,      0},
  {0x3F, true,   0,  7, 16,  0, 23, 59,  75000},
  // Agosto
  {0x40, true,  10,  8,  0,  0, 11,  0,  75000},
  {0x41, true,  10,  8, 11,  0, 15,  0,      0},
  {0x42, true,  10,  8, 15,  0, 23, 59,  75000},
  {0x43, true,   6,  8,  0,  0, 11,  0,  75000},
  {0x44, true,   6,  8, 11,  0, 16,  0,      0},
  {0x45, true,   6,  8, 16,  0, 23, 59,  75000},
  {0x46, true,   0,  8,  0,  0, 10,  0,  75000},
  {0x47, true,   0,  8, 10,  0, 16,  0,      0},
  {0x48, true,   0,  8, 16,  0, 23, 59,  75000},
  // Setembro
  {0x49, true,  10,  9,  0,  0, 13,  0,  75000},
  {0x4A, true,  10,  9, 13,  0, 14,  0,      0},
  {0x4B, true,  10,  9, 14,  0, 23, 59,  75000},
  {0x4C, true,   6,  9,  0,  0, 23, 59,  75000},
  {0x4D, true,   0,  9,  0,  0, 11,  0,  75000},
  {0x4E, true,   0,  9, 11,  0, 15,  0,      0},
  {0x4F, true,   0,  9, 15,  0, 23, 59,  75000},
  // Outubro
  {0x50, true,  10, 10,  0,  0, 23, 59,  75000},
  {0x51, true,   6, 10,  0,  0, 12,  0,  75000},
  {0x52, true,   6, 10, 12,  0, 14,  0,      0},
  {0x53, true,   6, 10, 14,  0, 23, 59,  75000},
  {0x54, true,   0, 10,  0,  0, 11,  0,  75000},
  {0x55, true,   0, 10, 11,  0, 14,  0,      0},
  {0x56, true,   0, 10, 14,  0, 23, 59,  75000},
  // Novembro
  {0x57, true,  10, 11,  0,  0, 23, 59,  75000},
  {0x58, true,   6, 11,  0,  0, 13,  0,  75000},
  {0x59, true,   6, 11, 13,  0, 14,  0,      0},
  {0x5A, true,   6, 11, 14,  0, 23, 59,  75000},
  {0x5B, true,   0, 11,  0,  0,  9,  0,  75000},
  {0x5C, true,   0, 11,  9,  0, 16,  0,      0},
  {0x5D, true,   0, 11, 16,  0, 23, 59,  75000},
  // Dezembro
  {0x5E, true,  10, 12,  0,  0, 10,  0,  75000},
  {0x5F, true,  10, 12, 10,  0, 14,  0,      0},
  {0x60, true,  10, 12, 14,  0, 15,  0,  75000},
  {0x61, true,  10, 12, 15,  0, 16,  0,      0},
  {0x62, true,  10, 12, 16,  0, 23, 59,  75000},
  {0x63, true,   6, 12,  0,  0, 11,  0,  75000},
  {0x64, true,   6, 12, 11,  0, 14,  0,      0},
  {0x65, true,   6, 12, 14,  0, 23, 59,  75000},
  {0x66, true,   0, 12,  0,  0, 10,  0,  75000},
  {0x67, true,   0, 12, 10,  0, 13,  0,      0},
  {0x68, true,   0, 12, 13,  0, 14,  0,  75000},
  {0x69, true,   0, 12, 14,  0, 15,  0,      0},
  {0x6A, true,   0, 12, 15,  0, 23, 59,  75000},
};

// =============================================================================
// =================== 6. LEITURA PERIÓDICA DE POTÊNCIA ========================
// =============================================================================

/*

  Apenas para verificação, essa rotina faz a leitura periódica de potência apenas para mostrar ao usuário se o programa ativo está sendo obedecido.
  Faz a leitura de um registrador do inversor via modbus e depois converte a unidade de W para kW e envia a informação via WebSockets

*/

// -----------------------------------------------------------------------------
// ------------------ Configurações de Leitor de Potencia ----------------------
// -----------------------------------------------------------------------------

enum class ReadPowerState : uint8_t{
  IDLE,
  REQUEST_MODBUS,
  WAIT_MODBUS,
  LOG,
  DONE,
  ERROR
};

struct ReadPowerService {

  // Config
  const uint32_t stepTimeoutMs = 800; // timeout por etapa (ajuste conforme sua rede)

  // Interno
  uint32_t stepStartMs = 0;
  bool pending = false;
  ReadPowerState state = ReadPowerState::IDLE;

  // Resultados
  bool modbusOk = false;
  bool modbusDone = false;
  bool dataOk = false;
  bool logOk = false;

  // Dados obtidos do inversor
  float power = 0.0f;
};

#endif // CONFIG_H