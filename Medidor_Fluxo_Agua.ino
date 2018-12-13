#include <EEPROM.h>
#include <Wire.h>
#include "RTClib.h"
#include <SPI.h>
#include <SD.h>
RTC_DS1307 rtc;

//Define se os comandos "log" serão printados no serial
const boolean EXIBIR_LOG = true;

//Define se a EEPROM pode ser usada
const boolean UTILIZAR_EEPROM = true;

const byte pinoSensorFluxo = 2; //Pino de entrada dos valores de fluxo de água
const byte pinoPushButton = 3;  //Pino de entrada do comando de transmissão por Bluetooth
const byte pinoLeitorSD = 53;   //Entrada conectada ao pino CS do modulo microSD
volatile int pulsosSensorFluxo; //A quantidade de pulsos precisa ser volátil para garantir que será atualizada corretamente durante as interrupções

File arquivoTXT;

float somatorioFluxo = 0.0f;
int segundosPassados = 0;
int minutosPassados = 0;

String horaInicioLinha = String();
String linhaDeDados = String();

void setup()
{
  pinMode(pinoSensorFluxo, INPUT);               //Seta o pino como entrada
  attachInterrupt(0, ContaPulsos, RISING);       //Configura a interrupção 0 para executar a função ContaPulsos (RISING: LOW -> HIGH)
  attachInterrupt(1, TransferirArquivo, CHANGE); //Configura a interrpução 1 para transmitir o arquivo por BluetoothS

  Serial.begin(9600); //Inicializa Serial

  Wire.begin();
  if (!rtc.begin())
  {
    log("Modulo RTC não encontrado...");
    while (1)
      ;
  }

  if (!rtc.isrunning())
  {
    log("RTC não esta sendo executado!");
  }
  //Utilizar as linhas abaixo para sincronizar o rtc, manualmente, ou usando __DATE__ e __TIME__
  //rtc.adjust(DateTime(2017, 4, 1, 13, 29, 5));
  //rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

  if (!SD.begin(pinoLeitorSD))
  {
    log("Modulo Cartao SD não foi iniciado!");
    return;
  }

  log(ObterDateTimeAtual() + " - Iniciando processamento...");
}

void loop()
{
  pulsosSensorFluxo = 0; // Resetando o contador para fazer a contagem por 1 segundo novamente
  interrupts();          //Ativa interrupções no Arduino
  delay(1000);           //Espera por 1 segundo (contabilizando os pulsos)
  noInterrupts();        //Desativa as interrupções para fazer o cálculo do segundo

  somatorioFluxo += CalcularFluxo();
  
  int val = digitalRead(pinoPushButton); // Lendo o pino controlado pelo Pushbutton
  if (val != HIGH)
  { // Se estiver sendo pressionado (LOW), transfere por Bluetooth
    TransferirArquivo();
  }

  //Verifica se alguma acao vai ser tomada (Escrever na EEPROM, ou enviar para o arquivo TXT)
  ExecutarAcao();
}

//Emite um Serial.println baseado numa variável global
void log(String texto)
{
  if (EXIBIR_LOG)
  {
    Serial.println(texto);
  }
}

//Cada pulso indica que 2.25mL de líquido passaram pelo sensor
void ContaPulsos()
{
  pulsosSensorFluxo++; //Toda vez que uma interrupção for ativada, a quantidade de pulsos é incrementada
}

float CalcularFluxo()
{
  log(String("Fluxo atual: " + String((pulsosSensorFluxo * 2.25) / 1000) + "L"));
  return ((pulsosSensorFluxo * 2.25) / 1000); //Calculando quantos litros passaram naquele segundo
}

//Executa acoes na EEPROM, ou no SD, caso seja atingido um momento especifico
void ExecutarAcao()
{

  if (segundosPassados == 0 && minutosPassados == 0)
  {
    //Obtendo o horário atual, para gravar na linha do arquivo
    horaInicioLinha = ObterDateTimeAtual();
  }

  segundosPassados++;

  if (segundosPassados == 60)
  {
    //Grava o somatório acumulado no minuto
    GravarEEPROM(minutosPassados, somatorioFluxo);

    somatorioFluxo = 0;
    segundosPassados = 0;
    minutosPassados++;

    if (minutosPassados == 60)
    {
      //Lendo os 60 registros referentes à uma hora
      linhaDeDados = horaInicioLinha + ';' + LerEEPROM(0, 60);
      log(linhaDeDados);

      //Gravar no SD
      GravarLinhaArquivo(linhaDeDados);
      segundosPassados = 0;
      minutosPassados = 0;
    }
  }
}

String ObterDateTimeAtual()
{
  String dataFormatada = "";
  interrupts(); //Ativa interrupções para capturar a hora atual
  DateTime now = rtc.now();
  noInterrupts(); //Desativa as interrupções para evitar que os pulsos aumentem

  int year = now.year();
  int month = now.month();
  int day = now.day();
  int hour = now.hour();
  int minute = now.minute();
  int second = now.second();

  //Padrão ISO 8601 (YYYY-MM-DD HH:mm:SS)  
  dataFormatada = printf("%4d-%02d-%02d %d:%02d:%02d", year, month, day, hour, minute, second);
  return dataFormatada;
}

void GravarEEPROM(int posicao, float valor)
{
  int posicaoFloat = posicao * sizeof(float);

  if (posicao <= EEPROM.length())
  {
    if (UTILIZAR_EEPROM)
      EEPROM.put(posicaoFloat, valor);

    log("Valor " + String(valor) + " escrito no endereco " + String(posicaoFloat) + "~" + String(posicaoFloat + sizeof(float)));
  }
}

String LerEEPROM(int posInicial, int posFinal)
{
  String textoLido = String();
  float valorResgatado = 0.0f;
  int posFinalFloat = posFinal * sizeof(float);

  for (int i = posInicial; i < posFinalFloat; i += sizeof(float))
  {
    if (UTILIZAR_EEPROM)
      EEPROM.get(i, valorResgatado);

    log("Valor " + String(valorResgatado) + " lido no endereco " + String(i) + "~" + String(i + sizeof(float)));
    textoLido += String(valorResgatado);

    if (textoLido != "")
    {
      textoLido += ";";
    }
  }
  return textoLido;
}

void GravarLinhaArquivo(String linhaDeDados)
{
  interrupts(); //Ativa interrupções para manipular cartao SD

  //Abre ou cria o arquivo, caso não exista
  arquivoTXT = SD.open("dados.txt", FILE_WRITE);

  //Se foi possivel abrir o arquivo, escreve a linha:
  if (arquivoTXT)
  {
    log("Escrevendo linha no arquivo dados.txt...");
    arquivoTXT.println(linhaDeDados);

    //Fechando o arquivo:
    arquivoTXT.close();
    log("Gravacao efetuada com sucesso!");
  }
  else
  {
    //Se o arquivo não pode ser aberto, printa erro:
    log("Erro ao abrir o arquivo dados.txt...");
  }
  noInterrupts(); //Desativa as interrupções para evitar que os pulsos aumentem
}

void TransferirArquivo()
{

  log("Abrindo arquivo para leitura...");

  if (!SD.begin(53))
  {
    log("Erro ao iniciar módulo SD!");
    return;
  }

  log("Módulo SD iniciado.");

  arquivoTXT = SD.open("dados.txt", FILE_READ);

  if (arquivoTXT)
  {
    //Envia o nome do arquivo como .csv para gravação no dispositivo de destino
    Serial1.println("#FNA#dados.csv#FNA#");

    //InicioConteudoArquivo
    Serial1.println("#SOF#");
    
    // Enquanto houver conteúdo para ler, transmite cada linha do arquivo:
    while (arquivoTXT.available())    
      Serial1.print(arquivoTXT.readStringUntil('\n'));    

    //FimConteudoArquivo
    Serial1.write("#EOF#");

    arquivoTXT.close();
  }
  else
  {
    log("Não foi possível abrir o arquivo dados.txt");
  }

  log("Transferência finalizada");
}
