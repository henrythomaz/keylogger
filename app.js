import express from "express";
import fs  from "fs";
import path from "path";
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const app = express();

const port = 3000;

app.use(express.json());

app.post("/envio", (req, res) => {
  const textoParaSalvar = req.body?.texto || "";

  if (!textoParaSalvar) {
    return res.status(400).json({ erro: "Nenhum texto para enviar." })
  }

  try {
    fs.appendFile('full-dados.txt', textoParaSalvar + '\n', 'utf8', (erro) => {
      if (erro) {
        console.error('Ocorreu um erro:', erro);
      }
      res.json({ msg: "Dados salvos!"});
    });
  } catch (erro) {
    console.error("Ocorreu um erro ao salvar", erro);
    res.status(500).json({ error: "Erro ao salvar no arquivo" })
  }


})

app.get("/", (req, res) => {
  const filePath = path.join(__dirname, 'full-dados.txt');

  fs.readFile(filePath, 'utf8', (err, data) => {
    if (err) {
      return res.status(500).json({ error: "Erro ao ler o arquivo" })
    }
    res.type('text/plain').send(data);
  });
});

app.listen(port, () => {
  console.log(`Rodando na porta: ${port}`);
});


// receber todos os dados filtrados na rota post e salvar em um arquivo txt que vai conter todos os dados de todos os pcs
// a rota get vai ler o txt de todos os dados
