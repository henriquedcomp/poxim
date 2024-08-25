#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <math.h>

// Tipo interrupção
typedef struct interrupcao
{
    uint8_t prioridade;
    uint32_t cr;
    uint32_t ipc;
    struct interrupcao *prox;
    struct interrupcao *ant;
} Interrupcao;

// Lista de interrupções agendadas
Interrupcao *interrupcoesAgendadas = NULL;

// 32 registradores inicializados com 0
uint32_t R[32] = {0};

// Output do terminal
char *outputTerminal = NULL;

// Define o tamanho máximo base do output
const int TAMANHO_BASE_OUTPUT = 300;

// Tamanho atual do output
int tamanhoOutput = 0;

// Memória indexada de 4 em 4 bytes
uint32_t *MEM = NULL;

// Ponteiros para os arquivos de entrada e saída
FILE *entrada = NULL;
FILE *saida = NULL;

// Ponteiro para arquivo de debug
FILE *debug = NULL;

// Flags do SR
typedef enum flag
{
    CY,
    IE,
    IV,
    OV,
    SN,
    ZD,
    ZN
} Flag;

// Registradores
typedef enum registrador_especial
{
    CR = 26,
    IPC,
    IR,
    PC,
    SP,
    SR
} RegistradorEspecial;

// Variável que determina se o programa está em execução
uint8_t emExecucao = 1;

// Contador do WatchDog
int contador;

// Registrador do temporizador (Watchdog)
uint32_t watchdog;

typedef union
{
    float f;
    uint32_t u;
} RegistradorFPU;

// Registradores do FPU
RegistradorFPU fpuX;
RegistradorFPU fpuY;
RegistradorFPU fpuZ;

// Variáveis auxiliares do FPU
uint8_t fpuX_IEEE754 = 0;
uint8_t fpuY_IEEE754 = 0;
uint8_t fpuZ_IEEE754 = 1;
uint8_t fpuControle = 0;
int fpuContador = -1;
uint8_t fpuPrioridade;

// Cadeia de caracteres da instrução
char instrucao[30] = {0};

// Strings de identificação de registradores
char registradorZ[5], registradorX[5], registradorY[5], registradorL[5], registradorV[5], registradorW[5];

// Variáveis auxiliares
uint32_t pcAtual;

// FUNÇÕES DO PROGRAMA

// Funções auxiliares
char *str_upper(char *);
int64_t potencia(int, int);
uint8_t empilhar(uint8_t);
uint8_t desempilhar(uint8_t);
void formatar_string_registrador(uint8_t, char *);
void formatar_string_empilhamento_instrucao(char *, uint8_t, char *);
void formatar_string_empilhamento_resultado_pt1(char *, uint8_t, uint32_t);
void formatar_string_empilhamento_resultado_pt2(char *, uint8_t, char *);
void inicializar_simulador();
void finalizar_simulador();
void retornar_instrucao_invalida();
void ativar_flag(Flag);
void desativar_flag(Flag);
uint8_t verificar_flag_setada(Flag);
void preparar_execucao_ISR();
void decodificar_instrucao(uint8_t);
void imprimir_output_terminal();
void adicionar_caractere_output(char);
Interrupcao *obter_interrupcao_duplicada(Interrupcao *);
void remover_interrupcao_agendada(Interrupcao *);
void agendar_interrupcao(uint8_t, uint32_t, uint32_t);
void tratar_interrupcao();
void destruir_interrupcoes_agendadas();
void executar_watchdog();
void decodificar_instrucao_fpu(uint8_t);
void fpu_adicao();
void fpu_subtracao();
void fpu_multiplicacao();
void fpu_divisao();
void fpu_atribuicao_x();
void fpu_atribuicao_y();
void fpu_teto();
void fpu_piso();
void fpu_arredondamento();
void fpu_interrupcao();
void fpu_preparar_interrupcao(uint8_t, int);
void executar_logica_fpu();

// Funções de debug
void visualizar_memoria();
void visualizar_registradores();
void visualizar_interrupcoes_pendentes();

// Operações
void _mov();
void _movs();
void _add();
void _sub();
void _mul();
void _sll();
void _muls();
void _sla();
void _div();
void _srl();
void _divs();
void _sra();
void _cmp();
void _and();
void _or();
void _not();
void _xor();
void _push();
void _pop();
void _addi();
void _subi();
void _muli();
void _divi();
void _modi();
void _cmpi();
void _l8();
void _l16();
void _l32();
void _s8();
void _s16();
void _s32();
void _callf();
void _ret();
void _reti();
void _cbr();
void _sbr();
void _bae();
void _bat();
void _bbe();
void _bbt();
void _beq();
void _bge();
void _bgt();
void _biv();
void _ble();
void _blt();
void _bne();
void _bni();
void _bnz();
void _bun();
void _bzd();
void _calls();
void _int();

int main(int argc, char *argv[])
{
    // INICIALIZANDO SIMULADOR

    // Ponteiros de entrada e saida inicializados com as respectivas permissões
    entrada = fopen(argv[1], "r");
    saida = fopen(argv[2], "w");

    // Ponteiro de debug inicializado
    debug = fopen("debug.txt", "w");

    inicializar_simulador();

    // Executa as instruções enquanto o programa não for interrompido
    while (emExecucao)
    {
        // Carregando a instrução de 32 bits (4 bytes) da memória indexada pelo PC (R29) no registrador IR (R28)
        R[IR] = MEM[R[PC] >> 2];

        // Obtendo o código da operação (6 bits mais significativos)
        uint8_t codOp = (R[IR] & (0b111111 << 26)) >> 26;

        // Definindo o pcAtual
        pcAtual = R[PC];

        // Decodificando a instrução buscada na memória
        decodificar_instrucao(codOp);

        // Verificando se o controle de interrupção está ligado e há interrupções pendentes
        if(verificar_flag_setada(IE) && interrupcoesAgendadas) {
            preparar_execucao_ISR();
            tratar_interrupcao();
        }

        // Lógica de implementação do watchdog
        if (watchdog & ((0b1 << 31) >> 31))
            executar_watchdog();

        // Lógica de implementação das operações do FPU
        if (fpuControle & 0b11111 && fpuContador == -1)
            decodificar_instrucao_fpu(fpuControle & 0b11111);

        // Contador do FPU
        if (fpuContador != -1)
            executar_logica_fpu();

        // PC = PC + 4 (próxima instrução)
        R[PC] = R[PC] + 4;
    }

    // FINALIZANDO SIMULADOR

    finalizar_simulador();

    // Retornando 0
    return 0;
}

void _mov()
{
    uint32_t xyl = R[IR] & 0x1FFFFF;
    uint8_t z = (R[IR] & (0b11111 << 21)) >> 21;

    R[z] = xyl;

    // R[0] não pode armazenar um valor diferente de 0
    R[0] = 0;

    // Formatação da saída
    formatar_string_registrador(z, registradorZ);

    sprintf(instrucao, "mov %s,%u", registradorZ, R[z]);
    fprintf(saida, "0x%08X:\t%-25s\t%s=0x%08X\n", pcAtual, instrucao, str_upper(registradorZ), xyl);
}

void _movs()
{
    uint32_t xyl = R[IR] & 0x1FFFFF;
    uint8_t x4 = (xyl & 0x100000) >> 20;
    uint8_t z = (R[IR] & (0b11111 << 21)) >> 21;

    if (x4)
        R[z] = xyl | 0xFFE00000;
    else
        R[z] = xyl;

    // R[0] não pode armazenar um valor diferente de 0
    R[0] = 0;

    // Formatação da saída
    formatar_string_registrador(z, registradorZ);

    sprintf(instrucao, "movs %s,%d", registradorZ, R[z]);
    fprintf(saida, "0x%08X:\t%-25s\t%s=0x%08X\n", pcAtual, instrucao, str_upper(registradorZ), (int32_t)R[z]);
}

void _add()
{
    uint8_t z = (R[IR] & (0b11111 << 21)) >> 21;
    uint8_t x = (R[IR] & (0b11111 << 16)) >> 16;
    uint8_t y = (R[IR] & (0b11111 << 11)) >> 11;

    R[z] = R[x] + R[y];

    // R[0] não pode armazenar um valor diferente de 0
    R[0] = 0;

    // Campos afetados
    if (R[z] == 0)
        ativar_flag(ZN);
    else
        desativar_flag(ZN);

    uint8_t rz31, rx31, ry31;
    rz31 = ((R[z] & 0x80000000) >> 31);
    if (rz31 == 1)
        ativar_flag(SN);
    else
        desativar_flag(SN);

    rx31 = ((R[x] & 0x80000000) >> 31);
    ry31 = ((R[y] & 0x80000000) >> 31);
    if (rx31 == ry31 && rz31 != rx31)
        ativar_flag(OV);
    else
        desativar_flag(OV);

    uint8_t rz32 = (((((uint64_t)R[x] + (uint64_t)R[y])) >> 32) & 0b1);
    if (rz32 == 1)
        ativar_flag(CY);
    else
        desativar_flag(CY);

    // Formatação da saída
    formatar_string_registrador(z, registradorZ);
    formatar_string_registrador(x, registradorX);
    formatar_string_registrador(y, registradorY);

    sprintf(instrucao, "add %s,%s,%s", registradorZ, registradorX, registradorY);
    fprintf(saida, "0x%08X:\t%-25s\t%s=%s+%s=0x%08X,SR=0x%08X\n",
            pcAtual,
            instrucao,
            str_upper(registradorZ),
            str_upper(registradorX),
            str_upper(registradorY),
            R[z],
            R[SR]);
}

void _sub()
{
    uint8_t z = (R[IR] & (0b11111 << 21)) >> 21;
    uint8_t x = (R[IR] & (0b11111 << 16)) >> 16;
    uint8_t y = (R[IR] & (0b11111 << 11)) >> 11;

    R[z] = R[x] - R[y];

    // R[0] não pode armazenar um valor diferente de 0
    R[0] = 0;

    // Campos afetados
    if (R[z] == 0)
        ativar_flag(ZN);
    else
        desativar_flag(ZN);

    uint8_t rz31, rx31, ry31;
    rz31 = ((R[z] & 0x80000000) >> 31);
    if (rz31 == 1)
        ativar_flag(SN);
    else
        desativar_flag(SN);

    rx31 = ((R[x] & 0x80000000) >> 31);
    ry31 = ((R[y] & 0x80000000) >> 31);
    if (rx31 != ry31 && rz31 != rx31)
        ativar_flag(OV);
    else
        desativar_flag(OV);

    uint8_t rz32 = (((((uint64_t)R[x] - (uint64_t)R[y])) >> 32) & 0b1);
    if (rz32 == 1)
        ativar_flag(CY);
    else
        desativar_flag(CY);

    // Formatação da saída
    formatar_string_registrador(z, registradorZ);
    formatar_string_registrador(x, registradorX);
    formatar_string_registrador(y, registradorY);

    sprintf(instrucao, "sub %s,%s,%s", registradorZ, registradorX, registradorY);
    fprintf(saida, "0x%08X:\t%-25s\t%s=%s-%s=0x%08X,SR=0x%08X\n",
            pcAtual,
            instrucao,
            str_upper(registradorZ),
            str_upper(registradorX),
            str_upper(registradorY),
            R[z],
            R[SR]);
}

void _mul()
{
    uint8_t z = (R[IR] & (0b11111 << 21)) >> 21;
    uint8_t x = (R[IR] & (0b11111 << 16)) >> 16;
    uint8_t y = (R[IR] & (0b11111 << 11)) >> 11;
    uint8_t l4_0 = R[IR] & 0b11111;

    R[l4_0] = (((uint64_t)R[x] * (uint64_t)R[y]) & (0xFFFFFFFF00000000)) >> 32;
    R[z] = R[x] * R[y];

    // R[0] não pode armazenar um valor diferente de 0
    R[0] = 0;

    // Campos afetados
    if (((((uint64_t)R[l4_0]) << 32) | R[z]) == 0)
        ativar_flag(ZN);
    else
        desativar_flag(ZN);

    if (R[l4_0] != 0)
        ativar_flag(CY);
    else
        desativar_flag(CY);

    // Formatação da saída
    formatar_string_registrador(z, registradorZ);
    formatar_string_registrador(x, registradorX);
    formatar_string_registrador(y, registradorY);
    formatar_string_registrador(l4_0, registradorL);

    sprintf(instrucao, "mul %s,%s,%s,%s", registradorL, registradorZ, registradorX, registradorY);
    fprintf(saida, "0x%08X:\t%-25s\t%s:%s=%s*%s=0x%016lX,SR=0x%08X\n",
            pcAtual,
            instrucao,
            str_upper(registradorL),
            str_upper(registradorZ),
            str_upper(registradorX),
            str_upper(registradorY),
            ((((uint64_t)(R[l4_0])) << 32) | R[z]),
            R[SR]);
}

void _sll()
{
    uint8_t z = (R[IR] & (0b11111 << 21)) >> 21;
    uint8_t x = (R[IR] & (0b11111 << 16)) >> 16;
    uint8_t y = (R[IR] & (0b11111 << 11)) >> 11;
    uint8_t l4_0 = R[IR] & 0b11111;
    uint64_t rzy = (((uint64_t)R[z]) << 32) | R[y];

    R[z] = ((rzy * (potencia(2, (l4_0 + 1)))) & (0xFFFFFFFF00000000)) >> 32;
    R[x] = (rzy * (potencia(2, (l4_0 + 1)))) & 0xFFFFFFFF;

    // R[0] não pode armazenar um valor diferente de 0
    R[0] = 0;

    // Campos afetados
    if (((((uint64_t)R[z]) << 32) | R[x]) == 0)
        ativar_flag(ZN);
    else
        desativar_flag(ZN);

    if (R[z] != 0)
        ativar_flag(CY);
    else
        desativar_flag(CY);

    // Formatação da saída
    formatar_string_registrador(z, registradorZ);
    formatar_string_registrador(x, registradorX);
    formatar_string_registrador(y, registradorY);

    sprintf(instrucao, "sll %s,%s,%s,%u", registradorZ, registradorX, registradorY, l4_0);
    fprintf(saida, "0x%08X:\t%-25s\t%s:%s=%s:%s<<%u=0x%016lX,SR=0x%08X\n",
            pcAtual,
            instrucao,
            str_upper(registradorZ),
            str_upper(registradorX),
            str_upper(registradorZ),
            str_upper(registradorY),
            l4_0 + 1,
            (((uint64_t)R[z]) << 32) | R[x],
            R[SR]);
}

void _muls()
{
    uint8_t z = (R[IR] & (0b11111 << 21)) >> 21;
    uint8_t x = (R[IR] & (0b11111 << 16)) >> 16;
    uint8_t y = (R[IR] & (0b11111 << 11)) >> 11;
    uint8_t l4_0 = R[IR] & 0b11111;
    int64_t rx64, ry64;

    rx64 = (int64_t)(int32_t)R[x];
    ry64 = (int64_t)(int32_t)R[y];

    R[l4_0] = ((rx64 * ry64) & 0xFFFFFFFF00000000) >> 32;
    R[z] = (rx64 * ry64) & 0xFFFFFFFF;

    // R[0] não pode armazenar um valor diferente de 0
    R[0] = 0;

    // Campos afetados
    if (((((int64_t)R[l4_0]) << 32) | R[z]) == 0)
        ativar_flag(ZN);
    else
        desativar_flag(ZN);

    if (R[l4_0] != 0)
        ativar_flag(OV);
    else
        desativar_flag(OV);

    // Formatação da saída
    formatar_string_registrador(z, registradorZ);
    formatar_string_registrador(x, registradorX);
    formatar_string_registrador(y, registradorY);
    formatar_string_registrador(l4_0, registradorL);

    sprintf(instrucao, "muls %s,%s,%s,%s", registradorL, registradorZ, registradorX, registradorY);
    fprintf(saida, "0x%08X:\t%-25s\t%s:%s=%s*%s=0x%016lX,SR=0x%08X\n",
            pcAtual,
            instrucao,
            str_upper(registradorL),
            str_upper(registradorZ),
            str_upper(registradorX),
            str_upper(registradorY),
            ((((int64_t)(int32_t)R[l4_0]) << 32) | R[z]),
            R[SR]);
}

void _sla()
{
    uint8_t z = (R[IR] & (0b11111 << 21)) >> 21;
    uint8_t x = (R[IR] & (0b11111 << 16)) >> 16;
    uint8_t y = (R[IR] & (0b11111 << 11)) >> 11;
    uint8_t l4_0 = R[IR] & 0b11111;
    int64_t rzy = (((int64_t)R[z]) << 32) | R[y];

    R[z] = ((rzy * (potencia(2, (l4_0 + 1)))) & 0xFFFFFFFF00000000) >> 32;
    R[x] = ((uint64_t)rzy * (potencia(2, (l4_0 + 1)))) & 0xFFFFFFFF;

    // R[0] não pode armazenar um valor diferente de 0
    R[0] = 0;

    // Campos afetados
    if (((((int64_t)R[z]) << 32) | R[x]) == 0)
        ativar_flag(ZN);
    else
        desativar_flag(ZN);

    if (R[z] != 0)
        ativar_flag(OV);
    else
        desativar_flag(OV);

    // Formatação da saída
    formatar_string_registrador(z, registradorZ);
    formatar_string_registrador(x, registradorX);
    formatar_string_registrador(y, registradorY);

    sprintf(instrucao, "sla %s,%s,%s,%u", registradorZ, registradorX, registradorY, l4_0);
    fprintf(saida, "0x%08X:\t%-25s\t%s:%s=%s:%s<<%u=0x%016lX,SR=0x%08X\n",
            pcAtual,
            instrucao,
            str_upper(registradorZ),
            str_upper(registradorX),
            str_upper(registradorZ),
            str_upper(registradorY),
            l4_0 + 1,
            (((int64_t)R[z]) << 32) | R[x],
            R[SR]);
}

void _div()
{
    uint8_t z = (R[IR] & (0b11111 << 21)) >> 21;
    uint8_t x = (R[IR] & (0b11111 << 16)) >> 16;
    uint8_t y = (R[IR] & (0b11111 << 11)) >> 11;
    uint8_t l4_0 = R[IR] & 0b11111;

    if (R[y])
    {
        R[l4_0] = R[x] % R[y];
        R[z] = R[x] / R[y];
    }

    // R[0] não pode armazenar um valor diferente de 0
    R[0] = 0;

    // Campos afetados
    if (R[z] == 0 && R[y])
        ativar_flag(ZN);
    else if (R[y])
        desativar_flag(ZN);

    if (R[y] == 0)
    {

        ativar_flag(ZD);

        if (verificar_flag_setada(IE))
        {
            preparar_execucao_ISR();
            R[CR] = 0;
            R[IPC] = R[PC];
            R[PC] = 0x00000008;
            R[PC] -= 4; // Será incrementado no fim da instrução, comportamento padrão
        }
    }
    else
        desativar_flag(ZD);

    if (R[l4_0] != 0)
        ativar_flag(CY);
    else if (R[y])
        desativar_flag(CY);

    // Formatação da saída
    formatar_string_registrador(z, registradorZ);
    formatar_string_registrador(x, registradorX);
    formatar_string_registrador(y, registradorY);
    formatar_string_registrador(l4_0, registradorL);

    sprintf(instrucao, "div %s,%s,%s,%s", registradorL, registradorZ, registradorX, registradorY);
    fprintf(saida, "0x%08X:\t%-25s\t%s=%s%%%s=0x%08X,%s=%s/%s=0x%08X,SR=0x%08X\n",
            pcAtual,
            instrucao,
            str_upper(registradorL),
            str_upper(registradorX),
            str_upper(registradorY),
            R[l4_0],
            str_upper(registradorZ),
            str_upper(registradorX),
            str_upper(registradorY),
            R[z],
            R[SR]);

    if (verificar_flag_setada(ZD) && verificar_flag_setada(IE)) {
        fprintf(saida, "[SOFTWARE INTERRUPTION]\n");
    }
}

void _srl()
{
    uint8_t z = (R[IR] & (0b11111 << 21)) >> 21;
    uint8_t x = (R[IR] & (0b11111 << 16)) >> 16;
    uint8_t y = (R[IR] & (0b11111 << 11)) >> 11;
    uint8_t l4_0 = R[IR] & 0b11111;
    uint64_t rzy = (((uint64_t)R[z]) << 32) | R[y];

    R[z] = ((rzy / (potencia(2, (l4_0 + 1)))) & (0xFFFFFFFF00000000)) >> 32;
    R[x] = (rzy / (potencia(2, (l4_0 + 1)))) & 0xFFFFFFFF;

    // R[0] não pode armazenar um valor diferente de 0
    R[0] = 0;

    // Campos afetados
    if (((((uint64_t)R[z]) << 32) | R[x]) == 0)
        ativar_flag(ZN);
    else
        desativar_flag(ZN);

    if (R[z] != 0)
        ativar_flag(CY);
    else
        desativar_flag(CY);

    // Formatação da saída
    formatar_string_registrador(z, registradorZ);
    formatar_string_registrador(x, registradorX);
    formatar_string_registrador(y, registradorY);

    sprintf(instrucao, "srl %s,%s,%s,%u", registradorZ, registradorX, registradorY, l4_0);
    fprintf(saida, "0x%08X:\t%-25s\t%s:%s=%s:%s>>%u=0x%016lX,SR=0x%08X\n",
            pcAtual,
            instrucao,
            str_upper(registradorZ),
            str_upper(registradorX),
            str_upper(registradorZ),
            str_upper(registradorY),
            l4_0 + 1,
            (((uint64_t)R[z]) << 32) | R[x],
            R[SR]);
}

void _divs()
{
    uint8_t z = (R[IR] & (0b11111 << 21)) >> 21;
    uint8_t x = (R[IR] & (0b11111 << 16)) >> 16;
    uint8_t y = (R[IR] & (0b11111 << 11)) >> 11;
    uint8_t l4_0 = R[IR] & 0b11111;

    if (R[y])
    {
        R[l4_0] = (int32_t)R[x] % (int32_t)R[y];
        R[z] = (int32_t)R[x] / (int32_t)R[y];
    }

    // R[0] não pode armazenar um valor diferente de 0
    R[0] = 0;

    // Campos afetados
    if (R[z] == 0 && R[y])
        ativar_flag(ZN);
    else if (R[y])
        desativar_flag(ZN);

    if (R[y] == 0)
    {

        ativar_flag(ZD);

        if (verificar_flag_setada(IE))
        {
            preparar_execucao_ISR();
            R[CR] = 0;
            R[IPC] = R[PC];
            R[PC] = 0x00000008;
            R[PC] -= 4; // Será incrementado no fim da instrução, comportamento padrão
        }
    }
    else
        desativar_flag(ZD);

    if (R[l4_0] != 0)
        ativar_flag(OV);
    else if (R[y])
        desativar_flag(OV);

    // Formatação da saída
    formatar_string_registrador(z, registradorZ);
    formatar_string_registrador(x, registradorX);
    formatar_string_registrador(y, registradorY);
    formatar_string_registrador(l4_0, registradorL);

    sprintf(instrucao, "divs %s,%s,%s,%s", registradorL, registradorZ, registradorX, registradorY);
    fprintf(saida, "0x%08X:\t%-25s\t%s=%s%%%s=0x%08X,%s=%s/%s=0x%08X,SR=0x%08X\n",
            pcAtual,
            instrucao,
            str_upper(registradorL),
            str_upper(registradorX),
            str_upper(registradorY),
            R[l4_0],
            str_upper(registradorZ),
            str_upper(registradorX),
            str_upper(registradorY),
            R[z],
            R[SR]);

    if (verificar_flag_setada(ZD) && verificar_flag_setada(IE)) {
        fprintf(saida, "[SOFTWARE INTERRUPTION]\n");
    }
}

void _sra()
{
    uint8_t z = (R[IR] & (0b11111 << 21)) >> 21;
    uint8_t x = (R[IR] & (0b11111 << 16)) >> 16;
    uint8_t y = (R[IR] & (0b11111 << 11)) >> 11;
    uint8_t l4_0 = R[IR] & 0b11111;
    int64_t rzy = (((int64_t)R[z]) << 32) | R[y];

    R[z] = ((rzy / (potencia(2, (l4_0 + 1)))) & 0xFFFFFFFF00000000) >> 32;
    R[x] = ((uint64_t)rzy / (potencia(2, (l4_0 + 1)))) & 0xFFFFFFFF;

    // R[0] não pode armazenar um valor diferente de 0
    R[0] = 0;

    // Campos afetados
    if (((((int64_t)R[z]) << 32) | R[x]) == 0)
        ativar_flag(ZN);
    else
        desativar_flag(ZN);

    if (R[z] != 0)
        ativar_flag(OV);
    else
        desativar_flag(OV);

    // Formatação da saída
    formatar_string_registrador(z, registradorZ);
    formatar_string_registrador(x, registradorX);
    formatar_string_registrador(y, registradorY);

    sprintf(instrucao, "sra %s,%s,%s,%u", registradorZ, registradorX, registradorY, l4_0);
    fprintf(saida, "0x%08X:\t%-25s\t%s:%s=%s:%s>>%u=0x%016lX,SR=0x%08X\n",
            pcAtual,
            instrucao,
            str_upper(registradorZ),
            str_upper(registradorX),
            str_upper(registradorZ),
            str_upper(registradorY),
            l4_0 + 1,
            (((int64_t)R[z]) << 32) | R[x],
            R[SR]);
}

void _cmp()
{
    uint8_t x = (R[IR] & (0b11111 << 16)) >> 16;
    uint8_t y = (R[IR] & (0b11111 << 11)) >> 11;

    uint64_t cmp = (uint64_t)R[x] - (uint64_t)R[y];

    // Campos afetados
    if (cmp == 0)
        ativar_flag(ZN);
    else
        desativar_flag(ZN);

    uint8_t cmp31 = (cmp & 0x80000000) >> 31;
    if (cmp31 == 1)
        ativar_flag(SN);
    else
        desativar_flag(SN);

    uint8_t rx31 = (R[x] & (0b1 << 31)) >> 31;
    uint8_t ry31 = (R[y] & (0b1 << 31)) >> 31;
    if (rx31 != ry31 && cmp31 != rx31)
        ativar_flag(OV);
    else
        desativar_flag(OV);

    uint8_t cmp32 = ((cmp >> 32) & 0b1);
    if (cmp32 == 1)
        ativar_flag(CY);
    else
        desativar_flag(CY);

    // Formatação da saída
    formatar_string_registrador(x, registradorX);
    formatar_string_registrador(y, registradorY);

    sprintf(instrucao, "cmp %s,%s", registradorX, registradorY);
    fprintf(saida, "0x%08X:\t%-25s\tSR=0x%08X\n", pcAtual, instrucao, R[SR]);
}

void _and()
{
    uint8_t z = (R[IR] & (0b11111 << 21)) >> 21;
    uint8_t x = (R[IR] & (0b11111 << 16)) >> 16;
    uint8_t y = (R[IR] & (0b11111 << 11)) >> 11;

    R[z] = R[x] & R[y];

    // R[0] não pode armazenar um valor diferente de 0
    R[0] = 0;

    // Campos afetados
    if (R[z] == 0)
        ativar_flag(ZN);
    else
        desativar_flag(ZN);

    uint8_t rz31 = (R[z] & (0b1 << 31)) >> 31;
    if (rz31 == 1)
        ativar_flag(SN);
    else
        desativar_flag(SN);

    // Formatação da saída
    formatar_string_registrador(z, registradorZ);
    formatar_string_registrador(x, registradorX);
    formatar_string_registrador(y, registradorY);

    sprintf(instrucao, "and %s,%s,%s", registradorZ, registradorX, registradorY);
    fprintf(saida, "0x%08X:\t%-25s\t%s=%s&%s=0x%08X,SR=0x%08X\n",
            pcAtual, instrucao,
            str_upper(registradorZ),
            str_upper(registradorX),
            str_upper(registradorY),
            R[z],
            R[SR]);
}

void _or()
{
    uint8_t z = (R[IR] & (0b11111 << 21)) >> 21;
    uint8_t x = (R[IR] & (0b11111 << 16)) >> 16;
    uint8_t y = (R[IR] & (0b11111 << 11)) >> 11;

    R[z] = R[x] | R[y];

    // R[0] não pode armazenar um valor diferente de 0
    R[0] = 0;

    // Campos afetados
    if (R[z] == 0)
        ativar_flag(ZN);
    else
        desativar_flag(ZN);

    uint8_t rz31 = (R[z] & (0b1 << 31)) >> 31;
    if (rz31 == 1)
        ativar_flag(SN);
    else
        desativar_flag(SN);

    // Formatação da saída
    formatar_string_registrador(z, registradorZ);
    formatar_string_registrador(x, registradorX);
    formatar_string_registrador(y, registradorY);

    sprintf(instrucao, "or %s,%s,%s", registradorZ, registradorX, registradorY);
    fprintf(saida, "0x%08X:\t%-25s\t%s=%s|%s=0x%08X,SR=0x%08X\n",
            pcAtual,
            instrucao,
            str_upper(registradorZ),
            str_upper(registradorX),
            str_upper(registradorY),
            R[z],
            R[SR]);
}

void _not()
{
    uint8_t z = (R[IR] & (0b11111 << 21)) >> 21;
    uint8_t x = (R[IR] & (0b11111 << 16)) >> 16;

    R[z] = ~R[x];

    // R[0] não pode armazenar um valor diferente de 0
    R[0] = 0;

    // Campos afetados
    if (R[z] == 0)
        ativar_flag(ZN);
    else
        desativar_flag(ZN);

    uint8_t rz31 = (R[z] & (0b1 << 31)) >> 31;
    if (rz31 == 1)
        ativar_flag(SN);
    else
        desativar_flag(SN);

    // Formatação da saída
    formatar_string_registrador(z, registradorZ);
    formatar_string_registrador(x, registradorX);

    sprintf(instrucao, "not %s,%s", registradorZ, registradorX);
    fprintf(saida, "0x%08X:\t%-25s\t%s=~%s=0x%08X,SR=0x%08X\n",
            pcAtual,
            instrucao,
            str_upper(registradorZ),
            str_upper(registradorX),
            R[z],
            R[SR]);
}

void _xor()
{
    uint8_t z = (R[IR] & (0b11111 << 21)) >> 21;
    uint8_t x = (R[IR] & (0b11111 << 16)) >> 16;
    uint8_t y = (R[IR] & (0b11111 << 11)) >> 11;

    R[z] = R[x] ^ R[y];

    // R[0] não pode armazenar um valor diferente de 0
    R[0] = 0;

    // Campos afetados
    if (R[z] == 0)
        ativar_flag(ZN);
    else
        desativar_flag(ZN);

    uint8_t rz31 = (R[z] & (0b1 << 31)) >> 31;
    if (rz31 == 1)
        ativar_flag(SN);
    else
        desativar_flag(SN);

    // Formatação da saída
    formatar_string_registrador(z, registradorZ);
    formatar_string_registrador(x, registradorX);
    formatar_string_registrador(y, registradorY);

    sprintf(instrucao, "xor %s,%s,%s", registradorZ, registradorX, registradorY);
    fprintf(saida, "0x%08X:\t%-25s\t%s=%s^%s=0x%08X,SR=0x%08X\n",
            pcAtual,
            instrucao,
            str_upper(registradorZ),
            str_upper(registradorX),
            str_upper(registradorY),
            R[z],
            R[SR]);
}

void _push()
{
    uint8_t v = (R[IR] & (0b11111 << 6)) >> 6;
    uint8_t w = R[IR] & 0b11111;
    uint8_t x = (R[IR] & (0b11111 << 16)) >> 16;
    uint8_t y = (R[IR] & (0b11111 << 11)) >> 11;
    uint8_t z = (R[IR] & (0b11111 << 21)) >> 21;

    uint8_t registradoresValidos = 0;
    char stringResultadoPt1[80] = {0};
    char stringResultadoPt2[30] = {0};
    char stringResultado[110] = {0};

    sprintf(instrucao, "push ");
    sprintf(stringResultadoPt1, "MEM[0x%08X]{", R[SP]);
    sprintf(stringResultadoPt2, "}={");

    if (empilhar(v))
    {
        registradoresValidos++;
        formatar_string_registrador(v, registradorV);
        formatar_string_empilhamento_instrucao(instrucao, registradoresValidos, registradorV);
        formatar_string_empilhamento_resultado_pt1(stringResultadoPt1, registradoresValidos, R[v]);
        formatar_string_empilhamento_resultado_pt2(stringResultadoPt2, registradoresValidos, registradorV);

        if (empilhar(w))
        {
            registradoresValidos++;
            formatar_string_registrador(w, registradorW);
            formatar_string_empilhamento_instrucao(instrucao, registradoresValidos, registradorW);
            formatar_string_empilhamento_resultado_pt1(stringResultadoPt1, registradoresValidos, R[w]);
            formatar_string_empilhamento_resultado_pt2(stringResultadoPt2, registradoresValidos, registradorW);

            if (empilhar(x))
            {
                registradoresValidos++;
                formatar_string_registrador(x, registradorX);
                formatar_string_empilhamento_instrucao(instrucao, registradoresValidos, registradorX);
                formatar_string_empilhamento_resultado_pt1(stringResultadoPt1, registradoresValidos, R[x]);
                formatar_string_empilhamento_resultado_pt2(stringResultadoPt2, registradoresValidos, registradorX);

                if (empilhar(y))
                {
                    registradoresValidos++;
                    formatar_string_registrador(y, registradorY);
                    formatar_string_empilhamento_instrucao(instrucao, registradoresValidos, registradorY);
                    formatar_string_empilhamento_resultado_pt1(stringResultadoPt1, registradoresValidos, R[y]);
                    formatar_string_empilhamento_resultado_pt2(stringResultadoPt2, registradoresValidos, registradorY);

                    if (empilhar(z))
                    {
                        registradoresValidos++;
                        formatar_string_registrador(z, registradorZ);
                        formatar_string_empilhamento_instrucao(instrucao, registradoresValidos, registradorZ);
                        formatar_string_empilhamento_resultado_pt1(stringResultadoPt1, registradoresValidos, R[z]);
                        formatar_string_empilhamento_resultado_pt2(stringResultadoPt2, registradoresValidos, registradorZ);
                    }
                }
            }
        }
    }

    if (registradoresValidos == 0)
        formatar_string_empilhamento_instrucao(instrucao, 0, NULL);

    sprintf(stringResultado, "%s%s}", stringResultadoPt1, stringResultadoPt2);
    fprintf(saida, "0x%08X:\t%-25s\t%s\n", pcAtual, instrucao, stringResultado);
}

void _pop()
{
    uint8_t v = (R[IR] & (0b11111 << 6)) >> 6;
    uint8_t w = R[IR] & 0b11111;
    uint8_t x = (R[IR] & (0b11111 << 16)) >> 16;
    uint8_t y = (R[IR] & (0b11111 << 11)) >> 11;
    uint8_t z = (R[IR] & (0b11111 << 21)) >> 21;

    uint8_t registradoresValidos = 0;
    char stringResultadoPt1[30] = {0};
    char stringResultadoPt2[80] = {0};
    char stringResultado[110] = {0};

    sprintf(instrucao, "pop ");
    sprintf(stringResultadoPt1, "{");
    sprintf(stringResultadoPt2, "}=MEM[0x%08X]{", R[SP]);

    if (desempilhar(v))
    {
        registradoresValidos++;
        formatar_string_registrador(v, registradorV);
        formatar_string_empilhamento_instrucao(instrucao, registradoresValidos, registradorV);
        formatar_string_empilhamento_resultado_pt1(stringResultadoPt2, registradoresValidos, R[v]);
        formatar_string_empilhamento_resultado_pt2(stringResultadoPt1, registradoresValidos, registradorV);

        if (desempilhar(w))
        {
            registradoresValidos++;
            formatar_string_registrador(w, registradorW);
            formatar_string_empilhamento_instrucao(instrucao, registradoresValidos, registradorW);
            formatar_string_empilhamento_resultado_pt1(stringResultadoPt2, registradoresValidos, R[w]);
            formatar_string_empilhamento_resultado_pt2(stringResultadoPt1, registradoresValidos, registradorW);

            if (desempilhar(x))
            {
                registradoresValidos++;
                formatar_string_registrador(x, registradorX);
                formatar_string_empilhamento_instrucao(instrucao, registradoresValidos, registradorX);
                formatar_string_empilhamento_resultado_pt1(stringResultadoPt2, registradoresValidos, R[x]);
                formatar_string_empilhamento_resultado_pt2(stringResultadoPt1, registradoresValidos, registradorX);

                if (desempilhar(y))
                {
                    registradoresValidos++;
                    formatar_string_registrador(y, registradorY);
                    formatar_string_empilhamento_instrucao(instrucao, registradoresValidos, registradorY);
                    formatar_string_empilhamento_resultado_pt1(stringResultadoPt2, registradoresValidos, R[y]);
                    formatar_string_empilhamento_resultado_pt2(stringResultadoPt1, registradoresValidos, registradorY);

                    if (desempilhar(z))
                    {
                        registradoresValidos++;
                        formatar_string_registrador(z, registradorZ);
                        formatar_string_empilhamento_instrucao(instrucao, registradoresValidos, registradorZ);
                        formatar_string_empilhamento_resultado_pt1(stringResultadoPt2, registradoresValidos, R[z]);
                        formatar_string_empilhamento_resultado_pt2(stringResultadoPt1, registradoresValidos, registradorZ);
                    }
                }
            }
        }
    }

    if (registradoresValidos == 0)
        formatar_string_empilhamento_instrucao(instrucao, 0, NULL);

    sprintf(stringResultado, "%s%s}", stringResultadoPt1, stringResultadoPt2);
    fprintf(saida, "0x%08X:\t%-25s\t%s\n", pcAtual, instrucao, stringResultado);
}

void _addi()
{
    uint8_t z = (R[IR] & (0b11111 << 21)) >> 21;
    uint8_t x = (R[IR] & (0b11111 << 16)) >> 16;
    int16_t i = R[IR] & 0xFFFF;
    uint8_t i15 = i >> 15;
    int32_t i15_i;

    if (i15 == 1)
    {
        i15_i = (int32_t)i | 0xffffffff;
    }
    else
    {
        i15_i = (int32_t)i;
    }

    R[z] = (int32_t)R[x] + i15_i;

    // R[0] não pode armazenar um valor diferente de 0
    R[0] = 0;

    // Campos afetados
    if (R[z] == 0)
        ativar_flag(ZN);
    else
        desativar_flag(ZN);

    uint8_t rz31, rx31;
    rz31 = ((R[z] & 0x80000000) >> 31);
    if (rz31 == 1)
        ativar_flag(SN);
    else
        desativar_flag(SN);

    rx31 = ((R[x] & 0x80000000) >> 31);
    if (rx31 == i15 && rz31 != rx31)
        ativar_flag(OV);
    else
        desativar_flag(OV);

    if (((((uint64_t)R[x] + (uint64_t)i15_i)) >> 32) == 1)
        ativar_flag(CY);
    else
        desativar_flag(CY);

    // Formatação da saída
    formatar_string_registrador(z, registradorZ);
    formatar_string_registrador(x, registradorX);

    sprintf(instrucao, "addi %s,%s,%d", registradorZ, registradorX, i15_i);
    fprintf(saida, "0x%08X:\t%-25s\t%s=%s+0x%08X=0x%08X,SR=0x%08X\n",
            pcAtual,
            instrucao,
            str_upper(registradorZ),
            str_upper(registradorX),
            i15_i,
            R[z],
            R[SR]);
}

void _subi()
{
    uint8_t z = (R[IR] & (0b11111 << 21)) >> 21;
    uint8_t x = (R[IR] & (0b11111 << 16)) >> 16;
    int16_t i = R[IR] & 0xFFFF;
    uint8_t i15 = i >> 15;
    int32_t i15_i;

    if (i15 == 1)
    {
        i15_i = (int32_t)i | 0xffffffff;
    }
    else
    {
        i15_i = (int32_t)i;
    }

    R[z] = (int32_t)R[x] - i15_i;

    // R[0] não pode armazenar um valor diferente de 0
    R[0] = 0;

    // Campos afetados
    if (R[z] == 0)
        ativar_flag(ZN);
    else
        desativar_flag(ZN);

    uint8_t rz31, rx31;
    rz31 = ((R[z] & 0x80000000) >> 31);
    if (rz31 == 1)
        ativar_flag(SN);
    else
        desativar_flag(SN);

    rx31 = ((R[x] & 0x80000000) >> 31);
    if (rx31 != i15 && rz31 != rx31)
        ativar_flag(OV);
    else
        desativar_flag(OV);

    if (((((uint64_t)R[x] - (uint64_t)i15_i)) >> 32) == 1)
        ativar_flag(CY);
    else
        desativar_flag(CY);

    // Formatação da saída
    formatar_string_registrador(z, registradorZ);
    formatar_string_registrador(x, registradorX);

    sprintf(instrucao, "subi %s,%s,%d", registradorZ, registradorX, i15_i);
    fprintf(saida, "0x%08X:\t%-25s\t%s=%s-0x%08X=0x%08X,SR=0x%08X\n",
            pcAtual,
            instrucao,
            str_upper(registradorZ),
            str_upper(registradorX),
            i15_i,
            R[z],
            R[SR]);
}

void _muli()
{
    uint8_t z = (R[IR] & (0b11111 << 21)) >> 21;
    uint8_t x = (R[IR] & (0b11111 << 16)) >> 16;
    int16_t i = R[IR] & 0xFFFF;
    uint8_t i15 = i >> 15;
    int32_t i15_i;

    if (i15 == 1)
    {
        i15_i = (int32_t)i | 0xffffffff;
    }
    else
    {
        i15_i = (int32_t)i;
    }

    R[z] = (int32_t)R[x] * i15_i;

    // R[0] não pode armazenar um valor diferente de 0
    R[0] = 0;

    // Campos afetados
    if (R[z] == 0)
        ativar_flag(ZN);
    else
        desativar_flag(ZN);

    int32_t rz63_32 = (((int64_t)((int32_t)R[x] * i15_i)) & 0xffffffff00000000) >> 32;
    if (rz63_32 != 0)
        ativar_flag(OV);
    else
        desativar_flag(OV);

    // Formatação da saída
    formatar_string_registrador(z, registradorZ);
    formatar_string_registrador(x, registradorX);

    sprintf(instrucao, "muli %s,%s,%d", registradorZ, registradorX, i15_i);
    fprintf(saida, "0x%08X:\t%-25s\t%s=%s*0x%08X=0x%08X,SR=0x%08X\n",
            pcAtual,
            instrucao,
            str_upper(registradorZ),
            str_upper(registradorX),
            i15_i,
            R[z],
            R[SR]);
}

void _divi()
{
    uint8_t z = (R[IR] & (0b11111 << 21)) >> 21;
    uint8_t x = (R[IR] & (0b11111 << 16)) >> 16;
    int16_t i = R[IR] & 0xFFFF;
    uint8_t i15 = i >> 15;
    int32_t i15_i;

    if (i15 == 1)
    {
        i15_i = (int32_t)i | 0xffffffff;
    }
    else
    {
        i15_i = (int32_t)i;
    }

    if (i != 0)
    {
        R[z] = (int32_t)R[x] / i15_i;
    }

    // R[0] não pode armazenar um valor diferente de 0
    R[0] = 0;

    // Campos afetados
    if (R[z] == 0 && i != 0)
        ativar_flag(ZN);
    else if (i != 0)
        desativar_flag(ZN);

    if (i == 0)
    {

        ativar_flag(ZD);

        if (verificar_flag_setada(IE))
        {
            preparar_execucao_ISR();
            R[CR] = 0;
            R[IPC] = R[PC];
            R[PC] = 0x00000008;
            R[PC] -= 4; // Será incrementado no fim da instrução, comportamento padrão
        }
    }
    else
        desativar_flag(ZD);

    desativar_flag(OV);

    // Formatação da saída
    formatar_string_registrador(z, registradorZ);
    formatar_string_registrador(x, registradorX);

    sprintf(instrucao, "divi %s,%s,%d", registradorZ, registradorX, i15_i);
    fprintf(saida, "0x%08X:\t%-25s\t%s=%s/0x%08X=0x%08X,SR=0x%08X\n",
            pcAtual,
            instrucao,
            str_upper(registradorZ),
            str_upper(registradorX),
            i15_i,
            R[z],
            R[SR]);

    if (verificar_flag_setada(ZD) && verificar_flag_setada(IE)) {
        fprintf(saida, "[SOFTWARE INTERRUPTION]\n");
    }
}

void _modi()
{
    uint8_t z = (R[IR] & (0b11111 << 21)) >> 21;
    uint8_t x = (R[IR] & (0b11111 << 16)) >> 16;
    int16_t i = R[IR] & 0xFFFF;
    uint8_t i15 = i >> 15;
    int32_t i15_i;

    if (i15 == 1)
    {
        i15_i = (int32_t)i | 0xffffffff;
    }
    else
    {
        i15_i = (int32_t)i;
    }

    if (i != 0)
    {
        R[z] = (int32_t)R[x] % i15_i;
    }

    // R[0] não pode armazenar um valor diferente de 0
    R[0] = 0;

    // Campos afetados
    if (R[z] == 0 && i != 0)
        ativar_flag(ZN);
    else if (i != 0)
        desativar_flag(ZN);

    if (i == 0)
    {

        ativar_flag(ZD);
        if (verificar_flag_setada(IE))
        {
            preparar_execucao_ISR();
            R[CR] = 0;
            R[IPC] = R[PC];
            R[PC] = 0x00000008;
            R[PC] -= 4; // Será incrementado no fim da instrução, comportamento padrão
        }
    }
    else
        desativar_flag(ZD);

    desativar_flag(OV);

    // Formatação da saída
    formatar_string_registrador(z, registradorZ);
    formatar_string_registrador(x, registradorX);

    sprintf(instrucao, "modi %s,%s,%d", registradorZ, registradorX, i15_i);
    fprintf(saida, "0x%08X:\t%-25s\t%s=%s%%0x%08X=0x%08X,SR=0x%08X\n",
            pcAtual,
            instrucao,
            str_upper(registradorZ),
            str_upper(registradorX),
            i15_i,
            R[z],
            R[SR]);

    if (verificar_flag_setada(ZD) && verificar_flag_setada(IE)) {
        fprintf(saida, "[SOFTWARE INTERRUPTION]\n");
    }
}

void _cmpi()
{
    uint8_t x = (R[IR] & (0b11111 << 16)) >> 16;
    int16_t i = R[IR] & 0xFFFF;
    uint8_t i15 = i >> 15;
    int32_t i15_i;

    if (i15 == 1)
    {
        i15_i = (int32_t)i | 0xffffffff;
    }
    else
    {
        i15_i = (int32_t)i;
    }

    int64_t cmpi = (int32_t)R[x] - i15_i;

    // Campos afetados
    if (cmpi == 0)
        ativar_flag(ZN);
    else
        desativar_flag(ZN);

    uint8_t cmpi31 = (cmpi & 0x80000000) >> 31;
    if (cmpi31 == 1)
        ativar_flag(SN);
    else
        desativar_flag(SN);

    uint8_t rx31 = (R[x] & (0b1 << 31)) >> 31;
    if (rx31 != i15 && cmpi31 != rx31)
        ativar_flag(OV);
    else
        desativar_flag(OV);

    uint8_t cmpi32 = (cmpi & 0x100000000) >> 32;
    if (cmpi32 == 1)
        ativar_flag(CY);
    else
        desativar_flag(CY);

    // Formatação da saída
    formatar_string_registrador(x, registradorX);

    sprintf(instrucao, "cmpi %s,%d", registradorX, i15_i);
    fprintf(saida, "0x%08X:\t%-25s\tSR=0x%08X\n", pcAtual, instrucao, R[SR]);
}

void _l8()
{
    uint8_t z = (R[IR] & (0b11111 << 21)) >> 21;
    uint8_t x = (R[IR] & (0b11111 << 16)) >> 16;
    int16_t i = R[IR] & 0xFFFF;
    uint32_t endereco = R[x] + i;

    // TODO: IMPLEMENTAR LEITURA NO TERMINAL

    if (endereco == 0x8080888F)
    {
        R[z] = fpuControle;
    }
    else
    {
        R[z] = ((uint8_t *)(&MEM[(endereco) >> 2]))[3 - ((endereco) % 4)];
    }

    // R[0] não pode armazenar um valor diferente de 0
    R[0] = 0;

    // Formatação da saída
    formatar_string_registrador(z, registradorZ);
    formatar_string_registrador(x, registradorX);

    sprintf(instrucao, "l8 %s,[%s%s%d]", registradorZ, registradorX, (i >= 0) ? ("+") : (""), i);
    fprintf(saida, "0x%08X:\t%-25s\t%s=MEM[0x%08X]=0x%02X\n", pcAtual, instrucao, str_upper(registradorZ), endereco, R[z]);
}

void _l16()
{
    uint8_t z = (R[IR] & (0b11111 << 21)) >> 21;
    uint8_t x = (R[IR] & (0b11111 << 16)) >> 16;
    int16_t i = R[IR] & 0xFFFF;

    R[z] = ((uint16_t *)(&MEM[(R[x] + i) >> 1]))[1 - ((R[x] + i) % 2)];

    // R[0] não pode armazenar um valor diferente de 0
    R[0] = 0;

    // Formatação da saída
    formatar_string_registrador(z, registradorZ);
    formatar_string_registrador(x, registradorX);

    sprintf(instrucao, "l16 %s,[%s%s%d]", registradorZ, registradorX, (i >= 0) ? ("+") : (""), i);
    fprintf(saida, "0x%08X:\t%-25s\t%s=MEM[0x%08X]=0x%04X\n", pcAtual, instrucao, str_upper(registradorZ), (R[x] + i) << 1, R[z]);
}

void _l32()
{
    uint8_t z = (R[IR] & (0b11111 << 21)) >> 21;
    uint8_t x = (R[IR] & (0b11111 << 16)) >> 16;
    int16_t i = R[IR] & 0xFFFF;
    uint32_t endereco = (R[x] + i) << 2;

    if (endereco == 0x80808880)
    {
        R[z] = fpuX_IEEE754 ? fpuX.u : fpuX.f;
    }
    else if (endereco == 0x80808884)
    {
        R[z] = fpuY_IEEE754 ? fpuY.u : fpuY.f;
    }
    else if (endereco == 0x80808888)
    {
        if (fpuZ_IEEE754)
        {
            memcpy(&R[z], &fpuZ.u, sizeof(uint32_t));
        }
        else
        {
            R[z] = fpuZ.f;
        }
    }
    else if (endereco == 0x8080888C)
    {
        R[z] = fpuControle;
    }
    else
    {
        R[z] = MEM[R[x] + i];
    }

    // R[0] não pode armazenar um valor diferente de 0
    R[0] = 0;

    // Formatação da saída
    formatar_string_registrador(z, registradorZ);
    formatar_string_registrador(x, registradorX);

    sprintf(instrucao, "l32 %s,[%s%s%d]", registradorZ, registradorX, (i >= 0) ? ("+") : (""), i);
    fprintf(saida, "0x%08X:\t%-25s\t%s=MEM[0x%08X]=0x%08X\n", pcAtual, instrucao, str_upper(registradorZ), endereco, R[z]);
}

void _s8()
{
    uint8_t z = (R[IR] & (0b11111 << 21)) >> 21;
    uint8_t x = (R[IR] & (0b11111 << 16)) >> 16;
    int16_t i = R[IR] & 0xFFFF;
    uint32_t endereco = R[x] + i;

    if (endereco == 0x8888888B)
    {
        adicionar_caractere_output((char)R[z]);
    }
    else if (endereco == 0x8080888F)
    {
        fpuControle = R[z];
    }
    else
    {
        ((uint8_t *)&MEM[(endereco) >> 2])[3 - (endereco) % 4] = (uint8_t)R[z];
    }

    // Formatação da saída
    formatar_string_registrador(z, registradorZ);
    formatar_string_registrador(x, registradorX);

    sprintf(instrucao, "s8 [%s%s%d],%s ", registradorX, (i >= 0) ? ("+") : (""), i, registradorZ);
    fprintf(saida, "0x%08X:\t%-25s\tMEM[0x%08X]=%s=0x%02X\n", pcAtual, instrucao, endereco, str_upper(registradorZ), (uint8_t)R[z]);
}

void _s16()
{
    uint8_t z = (R[IR] & (0b11111 << 21)) >> 21;
    uint8_t x = (R[IR] & (0b11111 << 16)) >> 16;
    int16_t i = R[IR] & 0xFFFF;

    ((uint16_t *)&MEM[(R[x] + i) >> 1])[1 - (R[x] + i) % 2] = (int16_t)R[z];

    // Formatação da saída
    formatar_string_registrador(z, registradorZ);
    formatar_string_registrador(x, registradorX);

    sprintf(instrucao, "s16 [%s%s%d],%s ", registradorX, (i >= 0) ? ("+") : (""), i, registradorZ);
    fprintf(saida, "0x%08X:\t%-25s\tMEM[0x%08X]=%s=0x%04X\n", pcAtual, instrucao, (R[x] + i) << 1, str_upper(registradorZ), R[z]);
}

void _s32()
{
    uint8_t z = (R[IR] & (0b11111 << 21)) >> 21;
    uint8_t x = (R[IR] & (0b11111 << 16)) >> 16;
    int16_t i = R[IR] & 0xFFFF;
    uint32_t endereco = (R[x] + i) << 2;

    if (endereco == 0x80808080)
    {
        watchdog = R[z];
        contador = watchdog & ~(0b1 << 31);
    }
    else if (endereco == 0x80808880)
    {
        fpuX.f = R[z];
        fpuX_IEEE754 = 0;
    }
    else if (endereco == 0x80808884)
    {
        fpuY.f = R[z];
        fpuY_IEEE754 = 0;
    }
    else if (endereco == 0x80808888)
    {
        fpuZ.f = R[z];
        fpuZ_IEEE754 = 1;
    }
    else if (endereco == 0x8080888C)
    {
        fpuControle = R[z] & (0b11111);
    }
    else
    {
        MEM[R[x] + i] = R[z];
    }

    // Formatação da saída
    formatar_string_registrador(z, registradorZ);
    formatar_string_registrador(x, registradorX);

    sprintf(instrucao, "s32 [%s%s%d],%s ", registradorX, (i >= 0) ? ("+") : (""), i, registradorZ);
    fprintf(saida, "0x%08X:\t%-25s\tMEM[0x%08X]=%s=0x%08X\n", pcAtual, instrucao, endereco, str_upper(registradorZ), R[z]);
}

void _callf()
{
    uint8_t x = (R[IR] & (0b11111 << 16)) >> 16;
    int16_t i = R[IR] & 0xFFFF;
    uint8_t i15 = i >> 15;
    int32_t i15_i;
    uint32_t spAtual = R[SP];

    if (i15 == 1)
    {
        i15_i = (int32_t)i | 0xffffffff;
    }
    else
    {
        i15_i = (int32_t)i;
    }

    MEM[R[SP] >> 2] = R[PC] + 4;
    R[SP] -= 4;
    R[PC] = ((int32_t)R[x] + i15_i) << 2;
    R[PC] -= 4; // Será incrementado no fim da instrução, comportamento padrão

    // Formatação da saída
    formatar_string_registrador(x, registradorX);

    sprintf(instrucao, "call [%s%s%d]", registradorX, (i15_i >= 0) ? ("+") : (""), i15_i);
    fprintf(saida, "0x%08X:\t%-25s\tPC=0x%08X,MEM[0x%08X]=0x%08X\n", pcAtual, instrucao, R[PC] + 4, spAtual, pcAtual + 4);
}

void _ret()
{
    R[SP] += 4;
    R[PC] = MEM[R[SP] >> 2];
    R[PC] -= 4; // Será incrementado no fim da instrução, comportamento padrão

    // Formatação da saída
    sprintf(instrucao, "ret");
    fprintf(saida, "0x%08X:\t%-25s\tPC=MEM[0x%08X]=0x%08X\n", pcAtual, instrucao, R[SP], R[PC] + 4);
}

void _reti()
{
    uint32_t sp_ipc, sp_cr;

    R[SP] += 4;
    sp_ipc = R[SP];
    R[IPC] = MEM[R[SP] >> 2];

    R[SP] += 4;
    sp_cr = R[SP];
    R[CR] = MEM[R[SP] >> 2];

    R[SP] += 4;
    R[PC] = MEM[R[SP] >> 2];

    R[PC] -= 4; // Será incrementado no fim da instrução, comportamento padrão

    // Formatação da saída
    sprintf(instrucao, "reti");
    fprintf(saida, "0x%08X:\t%-25s\tIPC=MEM[0x%08X]=0x%08X,CR=MEM[0x%08X]=0x%08X,PC=MEM[0x%08X]=0x%08X\n",
            pcAtual,
            instrucao,
            sp_ipc,
            R[IPC],
            sp_cr,
            R[CR],
            R[SP],
            R[PC] + 4);
}

void _cbr()
{
    uint8_t z = (R[IR] & (0b11111 << 21)) >> 21;
    uint8_t x = (R[IR] & (0b11111 << 16)) >> 16;

    R[z] = R[z] & ~(0b1 << x);

    // Formatação da saída
    formatar_string_registrador(z, registradorZ);

    sprintf(instrucao, "cbr %s[%u]", registradorZ, x);
    fprintf(saida, "0x%08X:\t%-25s\t%s=0x%08X\n", pcAtual, instrucao, str_upper(registradorZ), R[z]);
}

void _sbr()
{
    uint8_t z = (R[IR] & (0b11111 << 21)) >> 21;
    uint8_t x = (R[IR] & (0b11111 << 16)) >> 16;

    R[z] = R[z] | (0b1 << x);

    // R[0] não pode armazenar um valor diferente de 0
    R[0] = 0;

    // Formatação da saída
    formatar_string_registrador(z, registradorZ);

    sprintf(instrucao, "sbr %s[%u]", registradorZ, x);
    fprintf(saida, "0x%08X:\t%-25s\t%s=0x%08X\n", pcAtual, instrucao, str_upper(registradorZ), R[z]);
}

void _bae()
{
    int32_t i = R[IR] & 0x3ffffff;
    uint8_t i25 = i >> 25;
    int32_t i25_i = i;

    if (i25 == 1)
    {
        i25_i = i | 0xfc000000;
    }

    uint8_t cy = R[SR] & 0b1;

    if (cy == 0)
        R[PC] += i25_i << 2;

    // Formatação da saída
    sprintf(instrucao, "bae %d", i25_i);
    fprintf(saida, "0x%08X:\t%-25s\tPC=0x%08X\n", pcAtual, instrucao, R[PC] + 4);
}

void _bat()
{
    int32_t i = R[IR] & 0x3ffffff;
    uint8_t i25 = i >> 25;
    int32_t i25_i = i;

    if (i25 == 1)
    {
        i25_i = i | 0xfc000000;
    }

    uint8_t cy = R[SR] & 0b1;
    uint8_t zn = (R[SR] & (0b1 << 6)) >> 6;

    if (zn == 0 && cy == 0)
        R[PC] += i25_i << 2;

    // Formatação da saída
    sprintf(instrucao, "bat %d", i25_i);
    fprintf(saida, "0x%08X:\t%-25s\tPC=0x%08X\n", pcAtual, instrucao, R[PC] + 4);
}

void _bbe()
{
    int32_t i = R[IR] & 0x3ffffff;
    uint8_t i25 = i >> 25;
    int32_t i25_i = i;

    if (i25 == 1)
    {
        i25_i = i | 0xfc000000;
    }

    uint8_t cy = R[SR] & 0b1;
    uint8_t zn = (R[SR] & (0b1 << 6)) >> 6;

    if (zn == 1 || cy == 1)
        R[PC] += i25_i << 2;

    // Formatação da saída
    sprintf(instrucao, "bbe %d", i25_i);
    fprintf(saida, "0x%08X:\t%-25s\tPC=0x%08X\n", pcAtual, instrucao, R[PC] + 4);
}

void _bbt()
{
    int32_t i = R[IR] & 0x3ffffff;
    uint8_t i25 = i >> 25;
    int32_t i25_i = i;

    if (i25 == 1)
    {
        i25_i = i | 0xfc000000;
    }

    uint8_t cy = R[SR] & 0b1;

    if (cy == 1)
        R[PC] += i25_i << 2;

    // Formatação da saída
    sprintf(instrucao, "bbt %d", i25_i);
    fprintf(saida, "0x%08X:\t%-25s\tPC=0x%08X\n", pcAtual, instrucao, R[PC] + 4);
}

void _beq()
{
    int32_t i = R[IR] & 0x3ffffff;
    uint8_t i25 = i >> 25;
    int32_t i25_i = i;

    if (i25 == 1)
    {
        i25_i = i | 0xfc000000;
    }

    uint8_t zn = (R[SR] & (0b1 << 6)) >> 6;

    if (zn == 1)
        R[PC] += i25_i << 2;

    // Formatação da saída
    sprintf(instrucao, "beq %d", i25_i);
    fprintf(saida, "0x%08X:\t%-25s\tPC=0x%08X\n", pcAtual, instrucao, R[PC] + 4);
}

void _bge()
{
    int32_t i = R[IR] & 0x3ffffff;
    uint8_t i25 = i >> 25;
    int32_t i25_i = i;

    if (i25 == 1)
    {
        i25_i = i | 0xfc000000;
    }

    uint8_t sn = (R[SR] & (0b1 << 4)) >> 4;
    uint8_t ov = (R[SR] & (0b1 << 3)) >> 3;

    if (sn == ov)
        R[PC] += i25_i << 2;

    // Formatação da saída
    sprintf(instrucao, "bge %d", i25_i);
    fprintf(saida, "0x%08X:\t%-25s\tPC=0x%08X\n", pcAtual, instrucao, R[PC] + 4);
}

void _bgt()
{
    int32_t i = R[IR] & 0x3ffffff;
    uint8_t i25 = i >> 25;
    int32_t i25_i = i;

    if (i25 == 1)
    {
        i25_i = i | 0xfc000000;
    }

    uint8_t zn = (R[SR] & (0b1 << 6)) >> 6;
    uint8_t sn = (R[SR] & (0b1 << 4)) >> 4;
    uint8_t ov = (R[SR] & (0b1 << 3)) >> 3;

    if (zn == 0 && sn == ov)
        R[PC] += i25_i << 2;

    // Formatação da saída
    sprintf(instrucao, "bgt %d", i25_i);
    fprintf(saida, "0x%08X:\t%-25s\tPC=0x%08X\n", pcAtual, instrucao, R[PC] + 4);
}

void _biv()
{
    int32_t i = R[IR] & 0x3ffffff;
    uint8_t i25 = i >> 25;
    int32_t i25_i = i;

    if (i25 == 1)
    {
        i25_i = i | 0xfc000000;
    }

    uint8_t iv = (R[SR] & (0b1 << 2)) >> 2;

    if (iv)
        R[PC] += i25_i << 2;

    // Formatação da saída
    sprintf(instrucao, "biv %d", i25_i);
    fprintf(saida, "0x%08X:\t%-25s\tPC=0x%08X\n", pcAtual, instrucao, R[PC] + 4);
}

void _ble()
{
    int32_t i = R[IR] & 0x3ffffff;
    uint8_t i25 = i >> 25;
    int32_t i25_i = i;

    if (i25 == 1)
    {
        i25_i = i | 0xfc000000;
    }

    uint8_t zn = (R[SR] & (0b1 << 6)) >> 6;
    uint8_t sn = (R[SR] & (0b1 << 4)) >> 4;
    uint8_t ov = (R[SR] & (0b1 << 3)) >> 3;

    if (zn == 1 || sn != ov)
        R[PC] += i25_i << 2;

    // Formatação da saída
    sprintf(instrucao, "ble %d", i25_i);
    fprintf(saida, "0x%08X:\t%-25s\tPC=0x%08X\n", pcAtual, instrucao, R[PC] + 4);
}

void _blt()
{
    int32_t i = R[IR] & 0x3ffffff;
    uint8_t i25 = i >> 25;
    int32_t i25_i = i;

    if (i25 == 1)
    {
        i25_i = i | 0xfc000000;
    }

    uint8_t sn = (R[SR] & (0b1 << 4)) >> 4;
    uint8_t ov = (R[SR] & (0b1 << 3)) >> 3;

    if (sn != ov)
        R[PC] += i25_i << 2;

    // Formatação da saída
    sprintf(instrucao, "blt %d", i25_i);
    fprintf(saida, "0x%08X:\t%-25s\tPC=0x%08X\n", pcAtual, instrucao, R[PC] + 4);
}

void _bne()
{
    int32_t i = R[IR] & 0x3ffffff;
    uint8_t i25 = i >> 25;
    int32_t i25_i = i;

    if (i25 == 1)
    {
        i25_i = i | 0xfc000000;
    }

    uint8_t zn = (R[SR] & (0b1 << 6)) >> 6;

    if (zn == 0)
        R[PC] += i25_i << 2;

    // Formatação da saída
    sprintf(instrucao, "bne %d", i25_i);
    fprintf(saida, "0x%08X:\t%-25s\tPC=0x%08X\n", pcAtual, instrucao, R[PC] + 4);
}

void _bni()
{
    int32_t i = R[IR] & 0x3ffffff;
    uint8_t i25 = i >> 25;
    int32_t i25_i = i;

    if (i25 == 1)
    {
        i25_i = i | 0xfc000000;
    }

    uint8_t iv = (R[SR] & (0b1 << 2)) >> 2;

    if (iv == 0)
        R[PC] += i25_i << 2;

    // Formatação da saída
    sprintf(instrucao, "bni %d", i25_i);
    fprintf(saida, "0x%08X:\t%-25s\tPC=0x%08X\n", pcAtual, instrucao, R[PC] + 4);
}

void _bnz()
{
    int32_t i = R[IR] & 0x3ffffff;
    uint8_t i25 = i >> 25;
    int32_t i25_i = i;

    if (i25 == 1)
    {
        i25_i = i | 0xfc000000;
    }

    uint8_t zd = (R[SR] & (0b1 << 5)) >> 5;

    if (zd == 0)
        R[PC] += i25_i << 2;

    // Formatação da saída
    sprintf(instrucao, "bnz %d", i25_i);
    fprintf(saida, "0x%08X:\t%-25s\tPC=0x%08X\n", pcAtual, instrucao, R[PC] + 4);
}

void _bun()
{
    int32_t i = R[IR] & 0x3ffffff;
    uint8_t i25 = i >> 25;
    int32_t i25_i = i;

    if (i25 == 1)
    {
        i25_i = i | 0xfc000000;
    }

    R[PC] = R[PC] + (i25_i << 2);

    // Formatação da saída
    sprintf(instrucao, "bun %d", i25_i);
    fprintf(saida, "0x%08X:\t%-25s\tPC=0x%08X\n", pcAtual, instrucao, R[PC] + 4);
}

void _bzd()
{
    int32_t i = R[IR] & 0x3ffffff;
    uint8_t i25 = i >> 25;
    int32_t i25_i = i;

    if (i25 == 1)
    {
        i25_i = i | 0xfc000000;
    }

    uint8_t zd = (R[SR] & (0b1 << 5)) >> 5;

    if (zd)
        R[PC] += i25_i << 2;

    // Formatação da saída
    sprintf(instrucao, "bzd %d", i25_i);
    fprintf(saida, "0x%08X:\t%-25s\tPC=0x%08X\n", pcAtual, instrucao, R[PC] + 4);
}

void _calls()
{
    int32_t i = R[IR] & 0x3ffffff;
    uint8_t i25 = i >> 25;
    int32_t i25_i = i;
    uint32_t spAtual = R[SP];

    if (i25 == 1)
    {
        i25_i = i | 0xfc000000;
    }

    MEM[R[SP] >> 2] = R[PC] + 4;
    R[SP] -= 4;
    R[PC] += (i25_i << 2);

    // Formatação da saída
    sprintf(instrucao, "call %d", i25_i);
    fprintf(saida, "0x%08X:\t%-25s\tPC=0x%08X,MEM[0x%08X]=0x%08X\n", pcAtual, instrucao, R[PC] + 4, spAtual, pcAtual + 4);
}

void _int()
{
    uint32_t i = R[IR] & 0x3ffffff;

    if (!i)
        emExecucao = 0;
    else
    {
        preparar_execucao_ISR();
        R[CR] = i;
        R[IPC] = R[PC];
        R[PC] = 0x0000000C;
        R[PC] -= 4; // Será incrementado no fim da instrução, comportamento padrão
    }

    // Formatação da saída
    sprintf(instrucao, "int %u", i);
    fprintf(saida, "0x%08X:\t%-25s\tCR=0x%08X,PC=0x%08X\n", pcAtual, instrucao, i ? R[CR] : 0, i ? R[PC] + 4 : 0);

    if (i) {
        fprintf(saida, "[SOFTWARE INTERRUPTION]\n");
    }
}

uint8_t empilhar(uint8_t i)
{
    if (i != 0)
    {
        MEM[R[SP] >> 2] = R[i];
        R[SP] -= 4;

        return 1;
    }

    return 0;
}

uint8_t desempilhar(uint8_t i)
{
    if (i != 0)
    {
        R[SP] += 4;
        R[i] = MEM[R[SP] >> 2];

        return 1;
    }

    return 0;
}

void formatar_string_registrador(uint8_t indice, char *stringRegistrador)
{
    switch (indice)
    {
    case CR:
        sprintf(stringRegistrador, "cr");
        break;
    case IPC:
        sprintf(stringRegistrador, "ipc");
        break;
    case IR:
        sprintf(stringRegistrador, "ir");
        break;
    case PC:
        sprintf(stringRegistrador, "pc");
        break;
    case SP:
        sprintf(stringRegistrador, "sp");
        break;
    case SR:
        sprintf(stringRegistrador, "sr");
        break;
    default:
        sprintf(stringRegistrador, "r%u", indice);
    }
}

void formatar_string_empilhamento_instrucao(char *string, uint8_t rValidos, char *stringRegistrador)
{
    if (rValidos && stringRegistrador != NULL)
    {
        if (rValidos > 1)
            string = strcat(string, ",");

        string = strcat(string, stringRegistrador);
    }
    else
    {
        string = strcat(string, "-");
    }
}

void formatar_string_empilhamento_resultado_pt1(char *string, uint8_t rValidos, uint32_t valor)
{
    char hexa[15] = {0};

    sprintf(hexa, "0x%08X", valor);

    if (rValidos > 1)
        string = strcat(string, ",");
    string = strcat(string, hexa);
}

void formatar_string_empilhamento_resultado_pt2(char *string, uint8_t rValidos, char *stringRegistrador)
{
    if (rValidos > 1)
        string = strcat(string, ",");
    string = strcat(string, str_upper(stringRegistrador));
}

void inicializar_simulador()
{
    // 32KiB de memória inicializados com 0
    MEM = (uint32_t *)calloc(32, 1024);

    // Adicionando as instruções na memória
    uint32_t instrucao, i = 0;
    while (fscanf(entrada, "%X", &instrucao) != EOF)
    {
        MEM[i++] = instrucao;
    }

    // Alocando memória para o output do terminal
    outputTerminal = (char *)malloc(TAMANHO_BASE_OUTPUT * sizeof(char));

    // Inserindo mensagem de início de execução no arquivo de output
    fprintf(saida, "[START OF SIMULATION]\n");
}

void finalizar_simulador()
{
    if (tamanhoOutput)
        imprimir_output_terminal();

    // Inserindo mensagem de final de execução no arquivo de output
    fprintf(saida, "[END OF SIMULATION]\n");

    // Fechando arquivos de entrada e saída
    fclose(entrada);
    fclose(saida);

    // Fechando arquivo de debug
    fclose(debug);

    // Liberando memória alocada para o array de memória
    free(MEM);

    // Liberando memória alocada para o output do terminal
    free(outputTerminal);

    // Liberando memória alocada para armazenar as interrupções mascaráveis que ficaram pendentes
    destruir_interrupcoes_agendadas();
}

void imprimir_output_terminal()
{
    outputTerminal[tamanhoOutput + 1] = '\0';

    fprintf(saida, "[TERMINAL]\n");
    fprintf(saida, "%s", outputTerminal);
    fprintf(saida, "\n");
}

void adicionar_caractere_output(char caractere)
{
    outputTerminal[tamanhoOutput++] = caractere;

    if (tamanhoOutput && tamanhoOutput % TAMANHO_BASE_OUTPUT == 0)
    {
        int novoTamanho = (tamanhoOutput + TAMANHO_BASE_OUTPUT) * sizeof(char);
        outputTerminal = (char *)realloc(outputTerminal, novoTamanho);
    }
}

void retornar_instrucao_invalida()
{
    uint8_t ir31_26 = (R[IR] & (0b111111 << 26)) >> 26;
    // Exibindo mensagem de erro
    fprintf(saida, "[INVALID INSTRUCTION @ 0x%08X]\n", R[PC]);
    fprintf(saida, "[SOFTWARE INTERRUPTION]\n");
    preparar_execucao_ISR();

    ativar_flag(IV);
    R[CR] = ir31_26;
    R[IPC] = R[PC];
    R[PC] = 0x00000004;
    R[PC] -= 4; // Será incrementado no fim da instrução, comportamento padrão
}

void ativar_flag(Flag flag)
{
    R[SR] = R[SR] | (0b1 << flag);
}

void desativar_flag(Flag flag)
{
    R[SR] = R[SR] & ~(0b1 << flag);
}

uint8_t verificar_flag_setada(Flag flag)
{
    return (R[SR] & (0b1 << flag)) >> flag;
}

void preparar_execucao_ISR()
{
    MEM[R[SP] >> 2] = R[PC] + 4;
    R[SP] -= 4;

    MEM[R[SP] >> 2] = R[CR];
    R[SP] -= 4;

    MEM[R[SP] >> 2] = R[IPC];
    R[SP] -= 4;
}

Interrupcao *obter_interrupcao_duplicada(Interrupcao *interrupcao)
{
    Interrupcao *atual = interrupcoesAgendadas;

    while (atual)
    {
        if (atual->cr == interrupcao->cr && atual->prioridade == interrupcao->prioridade)
            return atual;

        atual = atual->prox;
    }

    return NULL;
}

void remover_interrupcao_agendada(Interrupcao *interrupcao)
{
    if (interrupcao->ant)
    {
        interrupcao->ant->prox = interrupcao->prox;
    }
    else
    {
        interrupcoesAgendadas = interrupcao->prox;
    }

    if (interrupcao->prox)
    {
        interrupcao->prox->ant = interrupcao->ant;
    }

    free(interrupcao);
}

void agendar_interrupcao(uint8_t prioridade, uint32_t cr, uint32_t ipc)
{
    Interrupcao *novaInterrupcao = (Interrupcao *)malloc(sizeof(Interrupcao));
    Interrupcao *atual = interrupcoesAgendadas;

    novaInterrupcao->prioridade = prioridade;
    novaInterrupcao->cr = cr;
    novaInterrupcao->ipc = ipc;
    novaInterrupcao->ant = NULL;
    novaInterrupcao->prox = NULL;

    if (obter_interrupcao_duplicada(novaInterrupcao))
    {
        remover_interrupcao_agendada(obter_interrupcao_duplicada(novaInterrupcao));
    }

    if(atual == NULL) {
        interrupcoesAgendadas = novaInterrupcao;
        return;
    }

    while (novaInterrupcao->prioridade > atual->prioridade)
    {
        if (atual->prox == NULL)
        {
            atual->prox = novaInterrupcao;
            novaInterrupcao->ant = atual;
            return;
        }

            atual = atual->prox;

    }

    if (atual->ant)
        atual->ant->prox = novaInterrupcao;

    atual->ant = novaInterrupcao;
    novaInterrupcao->ant = atual->ant;
    novaInterrupcao->prox = atual;
}

void tratar_interrupcao()
{
    R[CR] = interrupcoesAgendadas->cr;
    R[IPC] = interrupcoesAgendadas->ipc;

    switch(interrupcoesAgendadas->prioridade) {
        case 1: R[PC] = 0x10; break;
        case 2: 
            R[PC] = 0x14; // Seta o campo de status pra 1
            fpuControle = fpuControle | (0b1 << 5);
            break;
        case 3: R[PC] = 0x18; break;
        case 4: R[PC] = 0x1C; break;
    }

    if(interrupcoesAgendadas->prioridade > 1) {
        // Resetando a operação do registrador do FPU
        fpuControle = fpuControle & (0b1 << 5);
    }

    R[PC] -= 4; // Será incrementado no fim da instrução, comportamento padrão

    fprintf(saida, "[HARDWARE INTERRUPTION %u]\n", interrupcoesAgendadas->prioridade);

    remover_interrupcao_agendada(interrupcoesAgendadas);
}

void destruir_interrupcoes_agendadas()
{
    Interrupcao *ultimo = interrupcoesAgendadas;
    Interrupcao *anterior;

    if (ultimo == NULL)
        return;

    while (ultimo->prox)
    {
        ultimo = ultimo->prox;
    }

    anterior = ultimo->ant;

    while (interrupcoesAgendadas)
    {
        free(ultimo);

        ultimo = anterior;

        if (anterior)
            anterior = anterior->ant;
    }
}

void executar_watchdog()
{
    if (contador == 0)
    {
        watchdog = watchdog & 0;

        if (verificar_flag_setada(IE))
        {
            fprintf(saida, "[HARDWARE INTERRUPTION 1]\n");

            preparar_execucao_ISR();
            R[CR] = 0xE1AC04DA;
            R[IPC] = R[PC] + 4;
            R[PC] = 0x10;
            R[PC] -= 4; // Será incrementado no fim da instrução, comportamento padrão
        }
        else
        {
            agendar_interrupcao(1, 0xE1AC04DA, R[PC]);
        }

        return;
    }

    contador--;
}

void fpu_adicao()
{
    uint8_t expoenteX, expoenteY;

    fpuZ.f = fpuX.f + fpuY.f;

    expoenteX = (*(uint32_t *)&fpuX & (0b11111111 << 23)) >> 23;
    expoenteY = (*(uint32_t *)&fpuY & (0b11111111 << 23)) >> 23;

    fpu_preparar_interrupcao(3, abs(expoenteX - expoenteY) + 1);
}

void fpu_subtracao()
{
    uint8_t expoenteX, expoenteY;

    fpuZ.f = fpuX.f - fpuY.f;

    expoenteX = (*(uint32_t *)&fpuX & (0b11111111 << 23)) >> 23;
    expoenteY = (*(uint32_t *)&fpuY & (0b11111111 << 23)) >> 23;

    fpu_preparar_interrupcao(3, abs(expoenteX - expoenteY) + 1);
}

void fpu_multiplicacao()
{
    uint8_t expoenteX, expoenteY;

    fpuZ.f = fpuX.f * fpuY.f;

    expoenteX = (*(uint32_t *)&fpuX & (0b11111111 << 23)) >> 23;
    expoenteY = (*(uint32_t *)&fpuY & (0b11111111 << 23)) >> 23;

    fpu_preparar_interrupcao(3, abs(expoenteX - expoenteY) + 1);
}

void fpu_divisao()
{
    uint8_t expoenteX, expoenteY;

    expoenteX = (*(uint32_t *)&fpuX & (0b11111111 << 23)) >> 23;
    expoenteY = (*(uint32_t *)&fpuY & (0b11111111 << 23)) >> 23;

    if (fpuY.f == 0)
    {
        fpu_preparar_interrupcao(2, abs(expoenteX - expoenteY) + 1);

        return;
    }

    fpuZ.f = fpuX.f / fpuY.f;

    fpu_preparar_interrupcao(3, abs(expoenteX - expoenteY) + 1);
}

void fpu_atribuicao_x()
{
    fpuX.f = fpuZ.f;
    fpuX_IEEE754 = 1;

    fpu_preparar_interrupcao(4, 1);
}

void fpu_atribuicao_y()
{
    fpuY.f = fpuZ.f;
    fpuY_IEEE754 = 1;

    fpu_preparar_interrupcao(4, 1);
}

void fpu_teto()
{
    if (fpuZ_IEEE754)
        fpuZ.f = (float)(int)fpuZ.f == fpuZ.f ? fpuZ.f : (int)fpuZ.f + 1;
    else
        fpuZ.u = (float)(int)fpuZ.f == fpuZ.f ? fpuZ.f : (int)fpuZ.f + 1;

    fpuZ_IEEE754 = 0;

    fpu_preparar_interrupcao(4, 1);
}

void fpu_piso()
{
    if (fpuZ_IEEE754)
        fpuZ.f = (int)fpuZ.f;
    else
        fpuZ.u = (int)fpuZ.f;

    fpuZ_IEEE754 = 0;

    fpu_preparar_interrupcao(4, 1);
}

void fpu_arredondamento()
{
    int piso = (int)fpuZ.f;
    int teto = (float)piso == fpuZ.f ? piso : piso + 1;

    if (fpuZ.f - piso < teto - fpuZ.f)
    {
        fpuZ.f = piso;
    }
    else if (fpuZ.f - piso < teto - fpuZ.f)
    {
        fpuZ.f = teto;
    }
    else
    {
        if (piso % 2 == 0)
        {
            fpuZ.f = piso;
        }
        else
        {
            fpuZ.f = teto;
        }
    }

    fpuZ_IEEE754 = 0;

    fpu_preparar_interrupcao(4, 1);
}

void fpu_interrupcao()
{
    if (verificar_flag_setada(IE))
    {
        preparar_execucao_ISR();
        fprintf(saida, "[HARDWARE INTERRUPTION %u]\n", fpuPrioridade);
        R[CR] = 0x01EEE754;
        R[IPC] = pcAtual;

        switch (fpuPrioridade)
        {
        case 2:
            R[PC] = 0x14;
            // Seta o campo de status pra 1
            fpuControle = fpuControle | (0b1 << 5);
            break;
        case 3:
            R[PC] = 0x18;
            break;
        case 4:
            R[PC] = 0x1C;
            break;
        }

        R[PC] -= 4; // Será incrementado no fim da instrução, comportamento padrão

        // Resetando a operação do registrador do FPU
        fpuControle = fpuControle & (0b1 << 5);
    }
    else
    {
        agendar_interrupcao(fpuPrioridade, 0x01EEE754, R[PC]);
    }
}

void fpu_preparar_interrupcao(uint8_t prioridade, int contador)
{
    fpuContador = contador;
    fpuPrioridade = prioridade;
}

void executar_logica_fpu()
{
    if (fpuContador == 0)
        fpu_interrupcao();

    fpuContador--;
}

void decodificar_instrucao_fpu(uint8_t operacao)
{
    switch (operacao)
    {
    case 0b00001:
        fpu_adicao();
        break;
    case 0b00010:
        fpu_subtracao();
        break;
    case 0b00011:
        fpu_multiplicacao();
        break;
    case 0b00100:
        fpu_divisao();
        break;
    case 0b00101:
        fpu_atribuicao_x();
        break;
    case 0b00110:
        fpu_atribuicao_y();
        break;
    case 0b00111:
        fpu_teto();
        break;
    case 0b01000:
        fpu_piso();
        break;
    case 0b01001:
        fpu_arredondamento();
        break;
    default:
        fpu_preparar_interrupcao(2, 1);
    }
}

void decodificar_instrucao(uint8_t instrucao)
{
    // Código de diferenciação para instruções com o mesmo código de operação
    uint8_t codDif;

    switch (instrucao)
    {
    case 0b000000:
        _mov();
        break;
    case 0b000001:
        _movs();
        break;
    case 0b000010:
        _add();
        break;
    case 0b000011:
        _sub();
        break;
    case 0b000100:
        codDif = (R[IR] & (0b111 << 8)) >> 8;
        switch (codDif)
        {
        case 0b000:
            _mul();
            break;
        case 0b001:
            _sll();
            break;
        case 0b010:
            _muls();
            break;
        case 0b011:
            _sla();
            break;
        case 0b100:
            _div();
            break;
        case 0b101:
            _srl();
            break;
        case 0b110:
            _divs();
            break;
        case 0b111:
            _sra();
            break;
        default:
            retornar_instrucao_invalida();
        }
        break;
    case 0b000101:
        _cmp();
        break;
    case 0b000110:
        _and();
        break;
    case 0b000111:
        _or();
        break;
    case 0b001000:
        _not();
        break;
    case 0b001001:
        _xor();
        break;
    case 0b001010:
        _push();
        break;
    case 0b001011:
        _pop();
        break;
    case 0b010010:
        _addi();
        break;
    case 0b010011:
        _subi();
        break;
    case 0b010100:
        _muli();
        break;
    case 0b010101:
        _divi();
        break;
    case 0b010110:
        _modi();
        break;
    case 0b010111:
        _cmpi();
        break;
    case 0b011000:
        _l8();
        break;
    case 0b011001:
        _l16();
        break;
    case 0b011010:
        _l32();
        break;
    case 0b011011:
        _s8();
        break;
    case 0b011100:
        _s16();
        break;
    case 0b011101:
        _s32();
        break;
    case 0b011110:
        _callf();
        break;
    case 0b011111:
        _ret();
        break;
    case 0b100000:
        _reti();
        break;
    case 0b100001:
        codDif = (R[IR] & 0b1);
        switch (codDif)
        {
        case 0b0:
            _cbr();
            break;
        case 0b1:
            _sbr();
            break;
        default:
            retornar_instrucao_invalida();
        }
        break;
    case 0b101010:
        _bae();
        break;
    case 0b101011:
        _bat();
        break;
    case 0b101100:
        _bbe();
        break;
    case 0b101101:
        _bbt();
        break;
    case 0b101110:
        _beq();
        break;
    case 0b101111:
        _bge();
        break;
    case 0b110000:
        _bgt();
        break;
    case 0b110001:
        _biv();
        break;
    case 0b110010:
        _ble();
        break;
    case 0b110011:
        _blt();
        break;
    case 0b110100:
        _bne();
        break;
    case 0b110101:
        _bni();
        break;
    case 0b110110:
        _bnz();
        break;
    case 0b110111:
        _bun();
        break;
    case 0b111000:
        _bzd();
        break;
    case 0b111001:
        _calls();
        break;
    case 0b111111:
        _int();
        break;
    default:
        retornar_instrucao_invalida();
    }
}

void visualizar_memoria()
{
    fprintf(debug, "Exibindo dados da memória...\n\n");
    for (int i = 0; i < 8192; i++)
    {
        fprintf(debug, "0x%08X: 0x%08X", i * 4, MEM[i]);

        if ((i + 1) % 5 == 0)
            fprintf(debug, "\n");
        else
            fprintf(debug, "\t\t");
    }

    fprintf(debug, "\n");
}

void visualizar_registradores()
{
    fprintf(debug, "Exibindo dados dos registradores...\n\n");
    for (int i = 0; i < 32; i++)
    {
        fprintf(debug, "R[%d] = 0x%08x", i, R[i]);

        if ((i + 1) % 5 == 0)
            fprintf(debug, "\n");
        else
            fprintf(debug, "\t\t");
    }

    fprintf(debug, "\n");
}

char *str_upper(char *string)
{
    for (int i = 0; i < strlen(string); i++)
    {
        string[i] = toupper(string[i]);
    }

    return string;
}

int64_t potencia(int base, int expoente)
{
    long int resultado = 1;
    for (int i = 0; i < expoente; i++)
        resultado *= base;

    return resultado;
}

void visualizar_interrupcoes_pendentes()
{
    Interrupcao *atual = interrupcoesAgendadas;

    while (atual)
    {
        fprintf(debug, "Prioridade: %u\nCR: %u\nIPC: %u\n\n", atual->prioridade, atual->cr, atual->ipc);

        atual = atual->prox;
    }
}