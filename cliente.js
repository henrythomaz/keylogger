import fs from "fs";

const ARQUIVO = "dados_filtrados.txt";

console.log("Monitorando:", ARQUIVO);

let linhasEnviadas = new Set();
let ultimoConteudo = "";

setInterval(async () => {
    try {
        const conteudo = fs.readFileSync(ARQUIVO, "utf8");
       
        if (conteudo === ultimoConteudo || !conteudo.trim())
            return;
           
        ultimoConteudo = conteudo;
       
        const linhas = conteudo
            .split("\n")
            .map(l => l.trim())
            .filter(l => l.length > 0 && l.includes("][") && l.includes("@"));
       
        for (const linha of linhas) {
            if (linhasEnviadas.has(linha))
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
       
    } catch (erro) {
        console.error("Erro:", erro);
    }
}, 1000); 
