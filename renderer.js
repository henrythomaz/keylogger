const app = document.querySelector("#root");

async function pegarDados() {
  await fetch("https://requiring-edward-human-scale.trycloudflare.com")
    .then((resposta) => resposta.text())
    .then((textoBruto) => {
      const regex = /\[(.*?)\]/g;

      let match;

      app.innerHTML = "";

      while ((match = regex.exec(textoBruto)) !== null) {
        const valorLimpo = match[1];

        const paragrafo = document.createElement("p");

        paragrafo.textContent = valorLimpo;

        app.appendChild(paragrafo);
      }
    })
    .catch((erro) => {
      console.error("Erro ao renderizar dados:", erro);
    });
}

pegarDados();
