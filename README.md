# HDR-and-SDR-stream / HDR Bridge

Aplicativo nativo para Windows 11 que captura uma janela ou monitor em FP16 HDR e
oferece saída SDR com tone mapping na GPU ou HDR scRGB local, mantendo o HDR do
monitor ligado. Sem Electron, servidor ou navegador embutido. A versão 0.2 abre uma prévia pequena
dentro da janela principal, sem abrir automaticamente uma janela gigante.

## Configuração da captura

| Opção | Implementação |
| --- | --- |
| Resolução | Quadro de processamento em 720p (1280×720) ou 1080p (1920×1080), independente do tamanho da prévia. |
| Prévia | Imagem de 320×180 dentro do HDR Bridge, alimentada pelo quadro já processado. |
| Taxa de quadros | Alvo de 30 ou 60 fps, temporizador de alta resolução e prazos absolutos para evitar acumular o tempo de renderização. |
| Monitor | Lista cada monitor ativo, identificador `DISPLAY`, resolução e indicação de principal. |
| Aplicativo | Lista janelas visíveis por título e PID; selecione uma janela do aplicativo. |
| Cor | SDR com tone mapping ou HDR scRGB preservado na saída local. |
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
6. Durante o compartilhamento, mantenha essa saída aberta e sem minimizar. Pode
   esconder os controles pelo X e reabrir pelo ícone da bandeja.
7. **Fechar saída**, ou o X da janela de saída, fecha só essa janela; a prévia
   continua. **Parar** encerra a captura e apaga a imagem. **Sair** encerra o app.

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
com o serviço. Este aplicativo não implementa esse caminho nem o áudio.

A saída 1080p pode exceder a área útil de uma tela 1080p por causa das bordas.
Ela só aparece por ação explícita. Não é janela sempre no topo nem é trazida
para frente a cada frame. Se ambos os visores estiverem ocultos/minimizados, a
renderização pausa até algum deles voltar; a sessão de captura continua ativa.
A saída conserva as dimensões ao mudar de monitor/DPI; isso requer teste manual
em monitores com escalas diferentes.

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

- Sem áudio, câmera virtual ou integração com o encoder do Discord.
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
  sem leitura de frames para CPU no app. Desempenho ainda precisa de benchmark.

## Branches e testes

- [`main`](https://github.com/BozZf/HDR-and-SDR-stream/tree/main): aplicativo.
- [`discord-proposal`](https://github.com/BozZf/HDR-and-SDR-stream/tree/discord-proposal):
  aplicativo mais proposta em inglês e política HDR/SDR testável. Nada foi enviado ao Discord.

`PreviewPipelineTests` executa o processamento e a exibição reais no D3D11 WARP,
com quadros 720p/1080p em SDR/HDR e miniaturas de dois tamanhos. Verifica que os
pixels e dimensões do quadro completo não mudam, que a prévia não aplica tone
mapping novamente e que o conteúdo pode ser apagado.

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
