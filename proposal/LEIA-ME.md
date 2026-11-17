# Alternativa para sugerir ao Discord

Este branch propõe integrar a conversão **antes do encoder do Discord**, mantendo
o áudio e eliminando a necessidade de compartilhar uma janela intermediária.
HDR de ponta a ponta depende de negociação real; SDR é o fallback conservador.

Arquivos para anexar a uma sugestão:

- `FEEDBACK_EN.md`: texto curto em inglês, pronto para adaptar e enviar.
- `DISCORD_RFC.md`: proposta técnica em inglês, com limites e critérios de teste.
- `capture-policy.mjs` e `capture-policy.test.mjs`: referência executável das
  decisões HDR/SDR. Não são APIs existentes do Discord e não processam vídeo.

Os testes usam Node 20+ sem dependências: `node --test proposal/capture-policy.test.mjs`.
Este branch mantém o app de demonstração do branch `main`.

Nada foi enviado ao Discord. Preencha versão do cliente, Windows, GPU, monitor e
os passos de reprodução antes de reportar um bug. A proposta não afirma que a
versão atual do Discord tem ou não suporte HDR em todos os cenários.
