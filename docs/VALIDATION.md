# Validação no PC Windows

Nenhuma linha abaixo foi marcada como aprovada nesta entrega. Registre versão do
Windows/Discord, GPU/driver, monitor, modo HDR e origem ao executar.

| Cenário | Resultado esperado |
| --- | --- |
| Build Release x64 | Compila app e teste; CTest passa. |
| SDR e HDR no Windows | Lista de monitores/janelas e botão Iniciar capturam a origem selecionada. |
| Jogo HDR → saída SDR | Sol, nuvens e UI preservam detalhes; sem branco estourado generalizado. |
| Discord → amigo SDR | Amigo vê a saída SDR e confirma brilho/contraste; comparar com transmissão original. |
| Trocar SDR ↔ HDR | Sem travamento; estado de HDR local explícito; HDR não prometido na rede. |
| Exposição/branco | Alteram apenas SDR, inclusive em origem estática. |
| Redimensionar origem | Sem distorção; barras pretas mantêm proporção e saída conserva 720p/1080p. |
| Fechar origem | Captura para e saída some, sem continuar exibindo o último frame. |
| Alterar seleção sem Iniciar | Captura anterior continua; status conserva a origem ativa. |
| Esconder controles | Transmissão continua; ícone da bandeja reabre controles. |
| Fechar saída | Apenas a saída grande fecha; prévia e captura continuam. |
| Parar | Sessão termina; prévia, quadro interno e saída são apagados. |
| Minimizar saída | Só a apresentação dessa saída pausa; a prévia interna continua se visível. Evitar minimizar durante transmissão. |
| Monitor inteiro | Saída em segundo monitor; origem não contém a saída. |
| Alternar HDR / reconectar tela | Registrar eventuais falhas; reiniciar app em perda de dispositivo. |
| NVIDIA / AMD / Intel / GPUs distintas | Testar individualmente; sem alegar compatibilidade sem execução. |
| Privacidade | Seleção explícita + botão Iniciar e borda do Windows; controle excluído da captura; nenhuma conexão de rede. |

## Novos controles

- Iniciar captura: só a prévia de 320×180 abre dentro da janela principal; nenhuma
  saída grande é exibida automaticamente.
- Abrir/fechar saída para Discord: mesma imagem processada, sem mudança de origem
  ou cor; fechar pelo X não para a prévia.
- Executar `PreviewPipelineTests`: miniaturas 320×180/480×270 não alteram textura
  720p/1080p, SDR não recebe uma segunda conversão e HDR preserva valores.
- Ocultar controles durante compartilhamento: saída continua atualizando.
- Ambos os visores ocultos/minimizados: processamento pausa, retoma ao restaurar.
- Trocar 720p/1080p durante captura: conferir back buffer e área cliente de saída
  em 1280×720/1920×1080, inclusive com DPI diferente entre monitores.
- Trocar 30/60 fps: medir apresentações e distinguir o alvo da taxa real da origem.
- Lista de monitores: conferir primário, secundário, resolução e desconexão.
- Lista de aplicativos: janela correta, títulos repetidos diferenciados por PID,
  atualização após abrir/fechar apps e rejeição de origem obsoleta.
- Alterar seleção sem iniciar: origem ativa não muda silenciosamente.
- Trocar SDR/HDR em ambas as resoluções e taxas; ajustes SDR continuam funcionando.
- Executar `StreamSettingsTests`: dimensões, aspecto, prazos e descarte de atrasos.

## Medir desempenho

Comparar 1080p30, 1440p30, 4K30 e 1080p60, com mesma origem e mesmo serviço.
Medir CPU, memória privada e GPU por processo no Gerenciador de Tarefas, com e sem
o app. Registrar 60 segundos após estabilização, resolução da origem e saída,
driver, temperatura e latência ponta a ponta com um cronômetro filmado no destino.
Não publicar números de economia, uso de GPU ou latência sem medição real.

## Critério antes de distribuir binário

Build e CTest aprovados, teste de janela HDR real e transmissão remota SDR,
fechamento/redimensionamento testados, dependências/runtime documentados e
limitações de áudio/monitor inteiro visíveis. Assinatura de código e instalador
ficam para uma versão de distribuição; este pacote contém código-fonte.
