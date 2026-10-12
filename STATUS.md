# Status (2026-10-12)

## Pronto
- **Reescrita completa do jogo em C** (`src/`), sem emular o CPU: boot, acerto do relógio,
  ovo/eclosão, tela do pet com animação e overlays, os 7 ícones (comida, luz, jogo, remédio,
  banheiro, status, disciplina), eventos (dormir, chamadas, cocô, doença), evolução, morte/lápide,
  reset mantendo o relógio, teste de LCD de fábrica, som.
- **Jogável**: `./tama-nativo` (SDL2; ← ↓ → ou A S D; F = 10x; salva ao fechar em
  `~/.local/share/tamagotchi-p1/save.bin`; `-n` começa do zero). Com o app fechado o tempo para.
- **Validação contra o emulador** (`tools/validate.sh`): estado do jogo (RAM mapeada) igual
  em todos os scripts, exceto os casos abaixo; som igual; frames iguais um a um no boot/relógio,
  teste de fábrica e lápide→reset; no resto, mesmas telas/animações com ritmo ~1% diferente.

## Diferenças conhecidas (todas de *tempo*, não de lógica)
- O ritmo das telas é o do CPU original; a reescrita usa custos medidos por rotina e acerta
  ~1–2%. Consequências:
  - fase de animações de 2 quadros (cocô, "Z" do sono = 0x4A bits 0-2, contador 0x07E) pode
    sair invertida depois de animações com timeout;
  - o **jogo de adivinhar** usa o contador 0x05A (+1 por tick); o resultado depende do tick
    exato em que o botão é lido, então os sorteios diferem do emulador (o mecanismo é o mesmo);
  - pressões de botão a < 1 s uma da outra podem cair em voltas diferentes do laço (ex.:
    `t_care_light`); o pisca de 0,2 s do ícone de atenção na virada do minuto é uma corrida.
- Não testado: morte por velhice (anjo).

## Errata das notas de RE (re/notes) — já corrigida nas notas em 2026-10-12
- Botões em RAM: bit0=R, bit1=M, bit2=L (notas 0x000 e 0x800 dizem L=bit0).
- 0x5D = espécie, 0x50 = estágio; 0x4A bit3 = dormindo (não "morto"); evento 4 = cocô.
- O prog timer roda livre a 256/7 Hz (27,34 ms); a ISR de 1 Hz não reinicia a fase.
- Laço de limpeza do init (0x047) zera 16 nibbles por volta: o init limpa 0x000–0x0DF,
  0x100–0x26F e o LCD inteiro (notas 0x200 §2.4 erradas).
- Teste de fábrica, 1º passo: XP ainda é 2, então os ícones não acendem (bug do original).

## Ideias
- Tempo exato por instrução (para sorteios idênticos) exigiria contar ciclos do caminho real.
- Pet que continua vivendo com o app fechado; base para o pet com biologia/Needle/Nostr.
