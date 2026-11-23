# HDR-and-SDR-stream / HDR Bridge

Aplicativo nativo para Windows 11 que captura uma janela ou monitor em FP16 HDR e
oferece saída SDR com tone mapping na GPU ou HDR scRGB local, mantendo o HDR do
monitor ligado. A versão 1.0 tem interface com HTML/CSS/JavaScript,
prévia nativa dentro da janela principal e roteamento de áudio. A interface
vem embutida no executável e usa o WebView2 do Windows 11. Em máquinas sem
esse runtime, a interface Win32 abre automaticamente. Use **Tema** no topo
para alternar entre **Escuro** e **Claro**. A preferência vale nas duas
interfaces e é lembrada quando o app é reaberto.
Se os controles da interface moderna não aparecerem, pressione **Alt + Espaço**
na janela principal e escolha **Usar interface nativa**. Para voltar, escolha
**Usar interface moderna** no mesmo menu.

## Configuração da captura

| Opção | Implementação |
| --- | --- |
| Resolução | Quadro de processamento em 720p (1280×720) ou 1080p (1920×1080), independente do tamanho da prévia. |
| Prévia | Imagem de 320×180 dentro do HDR Bridge, alimentada pelo quadro já processado. |
| Taxa de quadros | Alvo de 30 ou 60 fps, temporizador de alta resolução e prazos absolutos para evitar acumular o tempo de renderização. |
| Monitor | Lista cada monitor ativo, identificador `DISPLAY`, resolução e indicação de principal. |
| Aplicativo | Lista janelas visíveis por título e PID; selecione uma janela do aplicativo. |
| Cor | SDR GPU, HDR scRGB local ou SDR de compatibilidade GDI para outros capturadores. |
| Áudio | Aplicativo capturado (árvore de processos) ou sistema, com volume e destino selecionável; desligado por padrão. |
| Ajustes SDR | Exposição e branco de referência manual; desabilitados em HDR. |

A origem só muda ao clicar em **Iniciar / trocar origem**. Resolução, fps e modo
de cor podem mudar durante a captura. **Atualizar lista** busca janelas abertas e
monitores conectados. O programa rejeita uma janela fechada ou monitor removido
antes de iniciar. O status mostra a origem ativa, a configuração do quadro e se a saída para o
Discord está aberta. Fechar essa saída não encerra a captura nem a prévia.

**Essas opções configuram a saída do app.** A resolução, taxa e formato efetivamente
transmitidos pelo Discord ou outro serviço continuam sujeitos às configurações e
limites desse serviço. O app não altera o cliente Discord.

## Compilar e abrir

Requisitos: Windows 11, driver Direct3D 11, Visual Studio 2022 com
**Desenvolvimento para desktop com C++**, Windows SDK 10.0.22621+ e CMake.
Na primeira compilação, o CMake baixa o SDK WebView2 e verifica seu SHA-256.
No Developer PowerShell do Visual Studio:

```powershell
git clone https://github.com/BozZf/HDR-and-SDR-stream.git
cd HDR-and-SDR-stream
.\build.ps1
.\build\Release\HdrBridge.exe
```

Ou execute sem o script PowerShell:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\HdrBridge.exe
```

O workflow [Windows](https://github.com/BozZf/HDR-and-SDR-stream/actions/workflows/windows.yml) compila, executa testes
e disponibiliza o executável como artefato quando a execução termina com sucesso.
Ele usa WARP para testar a matemática do shader; não testa um monitor HDR real.
O Visual C++ Redistributable x64 pode ser necessário em um PC sem Visual Studio.

## Compartilhar

1. Mantenha HDR ativo no Windows e no conteúdo desejado.
2. Escolha **Monitor** e qual monitor, ou **Aplicativo (janela)** e a janela.
3. Escolha **720p/1080p**, **30/60 fps** e **SDR/HDR**.
4. Clique em **Iniciar / trocar origem**. A imagem aparece na prévia pequena
   dentro do app. Nenhuma janela grande é aberta automaticamente.
5. Quando quiser compartilhar, clique em **Abrir saída para Discord**. Para amigos
   com tela SDR, use SDR e compartilhe **HDR Bridge - Saida SDR** no Discord.
   Ajuste a qualidade também no próprio serviço.
6. Passe o mouse no **canto superior direito** da saída para mostrar seus
   controles locais. Use **Recolher** para colocar a janela atrás das outras
   sem suspender o vídeo. O mesmo comando está na janela principal e em **F9**
   quando a saída está em foco. Clique em **Abrir saída para Discord** para
   trazê-la à frente.
7. **Fechar saída** ou **Esc** com a saída em foco fecha a janela e mantém a
   prévia. **Parar** encerra a captura. **Sair** encerra o app.

O **X da janela principal** pergunta se você quer minimizar para a bandeja ou
sair. Se marcar **Salvar minha escolha**, o X repete a ação nas próximas vezes.
Clique em **Perguntar ao fechar** para restaurar a confirmação. O botão
**Sair do Bridge** encerra o app e a captura mesmo se o X estiver configurado
para minimizar.

## Prévia pequena e resolução de transmissão

A conversão de cor e o redimensionamento geram primeiro uma textura GPU em
1280×720 ou 1920×1080. A miniatura apenas mostra uma cópia reduzida desse quadro;
ela não reduz a textura original. O segundo passe de exibição não aplica tone
mapping novamente, nem altera os pixels do quadro processado. Proporção e barras
pretas são preservadas.

**Isso mantém a resolução de processamento, não cria uma saída de vídeo virtual.**
O Discord ainda captura uma janela, e pode receber somente os pixels visíveis
dessa janela. Por isso a saída para compartilhar permanece separada e com a área
cliente na resolução escolhida. Compartilhar a miniatura não garante 1080p; os
controles e a prévia são excluídos de captura para evitar espelho e vazamento da
interface. Use o botão **Abrir saída para Discord** quando precisar transmitir.

Para transmitir o quadro completo sem manter essa janela grande aberta, ainda
seria necessário implementar uma saída por câmera virtual ou integração direta
com o serviço. Este aplicativo não implementa esse caminho de vídeo virtual. O áudio usa o
encaminhamento separado descrito abaixo.

A janela de saída mantém as dimensões escolhidas. Em um monitor de 1920×1080,
a saída em 1080p pode preencher a tela. Ela aparece ao clicar no botão de saída.
Se ambos os visores estiverem ocultos ou minimizados pelo Windows, a
renderização pausa até algum deles voltar; a sessão de captura continua ativa.
A saída conserva as dimensões ao mudar de monitor/DPI; isso requer teste manual
em monitores com escalas diferentes.

## Compartilhar somente a imagem

A saída tem uma janela sem moldura. Ao passar o mouse no canto superior direito,
o Windows exibe um painel local que o Bridge marca para exclusão de captura. A
fonte de vídeo continua com o mesmo título e contém somente a imagem. No Go Live
ou no ScreenShare, selecione essa janela na lista de aplicativos. Em OBS, use
**Captura de janela**. Teste **Windows Graphics Capture** se o capturador não
atualizar os quadros com a saída atrás de outras janelas.

O comando **Recolher** mantém a saída visível para captura e a coloca atrás das
outras janelas. Atalhos do Windows que minimizam todas as janelas podem suspender
a captura. Para verificar se o serviço acompanha a saída recolhida, confira a
imagem num espectador enquanto a origem estiver em movimento.

## Som em outros serviços

O botão **Áudio...** abre uma janela de configuração sem aumentar a janela
principal. O som fica **desligado por padrão**.

O Windows permite capturar o áudio por aplicativo via WASAPI sem driver extra.
Se o serviço já captura a fonte de áudio, use essa função diretamente. No OBS,
adicione **Captura de áudio de aplicativo** e selecione o jogo original,
mantendo a janela do HDR Bridge como fonte de vídeo. No ScreenShare 1.4.6,
**Áudio do sistema** captura a saída de som selecionada. A opção **Áudio da fonte**
segue a janela escolhida e, ao selecionar o Bridge, não seleciona o jogo original.
O cabo abaixo serve para entregar o áudio do Bridge quando o serviço espera um
dispositivo de entrada independente.

Em **Artifacts**, `HdrBridge-windows-x64-with-audio` inclui o app, o pacote
original VB-CABLE e [instruções de instalação](docs/AUDIO-SETUP.txt). O driver
é opcional. O Windows pede privilégios de administrador para a instalação e
o fornecedor orienta reiniciar depois. O artefato `HdrBridge-windows-x64`
contém o executável portátil.

1. Instale o driver na pasta **VB-CABLE** do pacote com áudio ou use um
   dispositivo virtual de áudio que já esteja instalado no Windows.
2. Em **Origem de áudio**, escolha **Aplicativo da janela capturada** ou
   **Sistema**. A primeira opção inclui os processos filhos do aplicativo e exige
   capturar uma janela. A segunda inclui outros aplicativos, notificações e
   chamadas, excluindo o processo do HDR Bridge para evitar realimentar sua saída.
3. Em **Destino**, escolha a ponta de reprodução do cabo virtual. Exemplo:
   `CABLE Input`. A lista contém dispositivos ativos do Windows; não há seleção
   automática. Não escolha seus alto-falantes/fones: isso pode duplicar o som local.
4. Clique em **Aplicar áudio**, inicie a captura de vídeo e veja o indicador de
   nível. Você pode ajustar o volume ou reaplicar durante a captura.
5. No Discord ou outro serviço, escolha a ponta de gravação correspondente como
   entrada de áudio. Exemplo: `CABLE Output`. O serviço precisa permitir selecionar
   esse dispositivo e ter permissão para capturar áudio.
6. **Parar**, fechar a origem ou sair do HDR Bridge encerra também o áudio.
   Fechar só a janela de saída de vídeo não encerra a captura de áudio. Selecionar
   **Desligado** e aplicar desliga apenas o áudio.

Isso é roteamento de áudio, não a inclusão automática do som no compartilhamento
de janela do serviço. Se o serviço tratar a entrada como microfone, o som pode
ser enviado enquanto você estiver na chamada, independentemente do compartilhamento
de vídeo. Supressão de ruído, ganho automático e cancelamento de eco do serviço
podem alterar música/jogos; ajuste conforme os controles disponíveis nele.
O aplicativo não mistura seu microfone real com o cabo. Para enviar ambos por uma
única entrada, é necessário um mixer externo ou suporte do próprio serviço.

O worker usa WASAPI process-loopback no Windows 11, PCM estéreo 48 kHz/16 bits e
renderização compartilhada com conversão do Windows para o destino. A fila é
limitada a 250 ms e descarta áudio antigo em sobrecarga; underruns geram silêncio.
Nenhuma reprodução adicional é enviada automaticamente aos seus fones. A fonte
original continua tocando normalmente. Um erro de áudio não interrompe o vídeo;
o status mostra o código e permite reaplicar após reconectar o dispositivo.
Não há ressincronização A/V adaptativa ou mistura de fontes nesta versão.

## Tela branca em outro app de transmissão

O HDR Bridge pode fornecer uma janela para outros serviços, mas cada um precisa
conseguir capturá-la. Não há integração específica com todos os serviços.

- Selecione **HDR Bridge - Saida SDR**, aberta pelo botão de saída. A janela
  principal e a miniatura são deliberadamente excluídas de capturas.
- Mantenha a saída aberta, sem minimizar, e comece pelo modo SDR.
- Se a saída normal não for capturada, escolha **SDR — compatibilidade GDI** e
  selecione a janela **HDR Bridge - Saida SDR - Compatibilidade** no serviço.
  Esse modo copia o quadro já corrigido para uma imagem BGRA8 desenhada com GDI,
  oferecendo uma alternativa a capturadores que não leem a apresentação Direct3D.
  O cursor da origem fica fora do vídeo nesse modo. O serviço que captura a saída
  pode acrescentar seu próprio cursor, conforme as opções de captura dele.
- O modo GDI é somente SDR e faz leitura GPU→CPU. Ele pode custar mais CPU,
  banda de memória e latência; teste primeiro em 720p/30 fps. O modo GPU normal
  continua sendo o padrão. A compatibilidade GDI não garante funcionamento em
  todos os capturadores, nem recupera conteúdo protegido.

A causa de um caso específico de tela branca só pode ser confirmada examinando
o capturador, a janela selecionada e o resultado na máquina. Para diagnóstico,
anexe o código/ZIP do outro app, o arquivo de dependências, os passos de
reprodução e indique se a prévia local e a janela de saída mostram a imagem.
Não são necessários senhas, tokens, arquivos de contas ou `node_modules`.

## HDR e SDR

A captura interna é sempre FP16 scRGB para não perder altas luzes antes da
conversão. O seletor controla **o tratamento da saída**, não o formato do monitor:

- **SDR:** ajuste de exposição/branco, compressão de luminância e saturação, curva
  sRGB e saída BGRA8 SDR.
- **HDR local:** preserva valores lineares scRGB, inclusive valores estendidos,
  em saída FP16. Não implementa encoder HDR10/PQ ou transmissão HDR pela rede.

Selecionar HDR não torna um serviço ou receptor compatível com HDR. A proposta
de integração do branch `discord-proposal` trata da negociação e fallback.
A janela intermediária SDR ainda precisa ser testada no caminho de captura real
do serviço; não há garantia de que ele não aplique outra conversão de cor.

## Limitações atuais

- Áudio entre serviços depende de um dispositivo de roteamento externo e da
  seleção da entrada no serviço. Sem câmera virtual ou integração com o encoder do Discord.
- Capturar monitor inteiro na mesma tela da janela de saída cria espelho. Mova a
  saída para outro monitor, ou prefira capturar a janela de um aplicativo.
- A saída permanece capturável; só os controles usam exclusão de captura.
- 30/60 fps são alvos. Origem, GPU, carga, serviço e receptor podem limitar a taxa.
- A origem continua na resolução original; selecionar 720p reduz a renderização
  de saída, mas uma origem 4K ainda exige captura e textura de origem 4K.
- Fonte minimizada ou conteúdo protegido pode congelar/ficar preto. Fechar a
  origem encerra a captura. Perda do dispositivo de vídeo exige reiniciar o app.
- O branco de referência começa em 203 nits, ajustável manualmente; não é uma
  medição do monitor. Sem leitura automática de referência SDR, metadados HDR10,
  calibração ICC ou gamut mapping perceptual completo.
- Arquitetura projetada para baixo custo: cópia de textura GPU e passe de shader,
  sem leitura de frames para CPU no modo GPU normal. O modo GDI usa leitura
  explícita para compatibilidade. Desempenho ainda precisa de benchmark.

## Branches e testes

- [`main`](https://github.com/BozZf/HDR-and-SDR-stream/tree/main): aplicativo.
- [`discord-proposal`](https://github.com/BozZf/HDR-and-SDR-stream/tree/discord-proposal):
  aplicativo mais proposta em inglês e política HDR/SDR testável. Nada foi enviado ao Discord.

`PreviewPipelineTests` executa o processamento e a exibição reais no D3D11 WARP,
com quadros 720p/1080p em SDR/HDR e miniaturas de dois tamanhos. Verifica que os
pixels e dimensões do quadro completo não mudam, que a prévia não aplica tone
mapping novamente e que o conteúdo pode ser apagado.

`AudioBufferTests` testa fila estéreo, saturação de ganho, silêncio, underrun,
descarte de atrasos e limpeza. Esses testes não validam um dispositivo físico ou
virtual de áudio. A escuta remota e o comportamento WASAPI ainda exigem teste
manual em um PC Windows com os dispositivos de destino. O teste de prévia também
verifica os pixels da leitura GDI e rejeita interpretar HDR como SDR.

`ColorPipelineTests` executa o HLSL de produção no Direct3D WARP e testa a faixa
SDR, neutralidade, altas luzes, exposição e preservação HDR. `StreamSettingsTests`
testa dimensões, proporção e temporização sem acumular atraso de renderização.
Este segundo teste também pode ser executado em Linux:

```sh
g++ -std=c++20 -Wall -Wextra -Werror -Isrc tests/stream_settings.cpp -o stream-settings-tests
./stream-settings-tests
```

Consulte [docs/VALIDATION.md](docs/VALIDATION.md) para a validação visual no Windows.
Testes automatizados não substituem captura real nem inspeção da imagem remota.

## Referências primárias

- [Microsoft: captura e orientação FP16 para HDR](https://learn.microsoft.com/en-us/windows/apps/develop/media-authoring-processing/screen-capture)
- [Microsoft: Advanced Color e scRGB](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/high-dynamic-range)
- [Microsoft: captura de monitor](https://learn.microsoft.com/en-us/windows/win32/api/windows.graphics.capture.interop/nf-windows-graphics-capture-interop-igraphicscaptureiteminterop-createformonitor)
- [Microsoft: captura de janela](https://learn.microsoft.com/en-us/windows/win32/api/windows.graphics.capture.interop/nf-windows-graphics-capture-interop-igraphicscaptureiteminterop-createforwindow)

- [Microsoft: Application Loopback](https://learn.microsoft.com/en-us/windows/win32/api/mmdeviceapi/nf-mmdeviceapi-activateaudiointerfaceasync)
- [Microsoft: exclusão de captura de janelas](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowdisplayaffinity)
