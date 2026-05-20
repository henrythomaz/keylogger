//
//
// // cliente junto com o arquivo main e os txt 
// // cliente vai se conectar no servidor e dar post dos resultados a cada linha adicionada no dados_filtrados.
//
// import fs from "fs";
//
// const ARQUIVO = "dados_filtrados.txt";
//
// console.log("Monitorando:", ARQUIVO);
//
// fs.watch(ARQUIVO, async (evento) => {
//
//     if (evento !== "change")
//         return;
//
//     try {
//
//         const conteudo = fs.readFileSync(ARQUIVO, "utf8").trim();
//
//         if (!conteudo)
//             return;
//
//         console.log("Enviando:");
//         console.log(conteudo);
//
//         // envia pro servidor
//         const response = await fetch("http://localhost:3000/envio", {
//             method: "POST",
//             headers: {
//                 "Content-Type": "application/json"
//             },
//             body: JSON.stringify({
//                 texto: conteudo
//             })
//         });
//
//         const data = await response.json();
//
//         console.log("Servidor:", data);
//
//         // limpa arquivo
//         fs.writeFileSync(ARQUIVO, "", "utf8");
//
//         console.log("Arquivo limpo.");
//
//     } catch (erro) {
//
//         console.error("Erro:", erro);
//
//     }
//
// });

import fs from "fs";

const ARQUIVO = "dados_filtrados.txt";

console.log("Monitorando:", ARQUIVO);

let linhasEnviadas = new Set();

setInterval(async () => {

    try {

        const conteudo = fs.readFileSync(ARQUIVO, "utf8");

        if (!conteudo.trim())
            return;

        const linhas = conteudo
            .split("\n")
            .map(l => l.trim())
            .filter(l => l.length > 0);

        for (const linha of linhas) {

            // evita reenviar
            if (linhasEnviadas.has(linha))
                continue;

            // verifica se parece completa
            if (!linha.includes("]["))
                continue;

            console.log("Enviando:", linha);

            await fetch("http://localhost:3000/envio", {
                method: "POST",
                headers: {
                    "Content-Type": "application/json"
                },
                body: JSON.stringify({
                    texto: linha
                })
            });

            linhasEnviadas.add(linha);
        }

        // mantém só linhas não enviadas
        const restantes = linhas.filter(
            l => !linhasEnviadas.has(l)
        );

        fs.writeFileSync(
            ARQUIVO,
            restantes.join("\n"),
            "utf8"
        );

    } catch (erro) {

        console.error("Erro:", erro);

    }

}, 1000);
