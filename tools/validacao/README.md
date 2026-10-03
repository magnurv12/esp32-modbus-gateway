# Validação do gateway

Roteiro de testes e dados brutos da validação descrita no TCC *Gateway Modbus
RTU para API RESTful baseado em ESP32* (cap. 4, "Resultados e discussão").

## Bancada

| Item | Configuração |
|---|---|
| Escravo | [Modbux](https://github.com/ploxc/modbux) 2.3.0 simulando a estação de bombeamento EB-01 (escravo 1) |
| Ligação ao barramento | adaptador USB/RS-485 genérico com conversor CH340 (USB 0x1A86:0x7523) |
| Barramento | 9600 bit/s, 8N1, par A/B de bancada |
| Gateway | ESP32-D0WD-V3, núcleo Arduino 2.0.17 / ESP-IDF 4.4.7, firmware `33dc973` |
| Rede | Wi-Fi 2,4 GHz; RSSI do gateway entre -79 e -71 dBm durante os testes |
| Cliente | este roteiro (Python 3.9, só biblioteca padrão), no computador da bancada |

O mapa de registradores da EB-01 está em
[modbus_gateway_app/tools/modbux/eb01-slave.json](https://github.com/magnurv12/modbus_gateway_app/blob/main/tools/modbux/eb01-slave.json).

## Como executar

Feche qualquer cliente WebSocket (o aplicativo) antes: as consultas
periódicas das assinaturas alteram o tempo de resposta e o caso CT10. O
roteiro verifica isso em `/api/health` e para se houver assinaturas.

```bash
python3 executar_testes.py dados/resultado.json          # CT01-CT10 (exceto CT07) e latência
python3 executar_testes.py dados/resultado.json CT07     # interativo: pare e religue o simulador
python3 analisar.py dados/resultado.json                 # tabela de tempo de resposta
```

O CT07 aguarda o escravo parar de responder, coleta 10 leituras e uma escrita
e espera a recuperação. As escritas usam endereços fora do mapa da planta
(>= 20) ou restauram o valor original.

| Caso | O que verifica |
|---|---|
| CT01 | leitura das quatro tabelas (FC 01-04), item e bloco |
| CT02 | escrita de um item (FC 05/06) e leitura de volta |
| CT03 | escrita em bloco (FC 15/16) e leitura de volta |
| CT04 | seleção do escravo pelo parâmetro `slave` |
| CT05 | 15 requisições inválidas: 400/404/405/413 sem tráfego no barramento |
| CT06 | exceção Modbus 02 convertida em 404 |
| CT07 | escravo sem resposta: 504 após o tempo limite |
| CT08 | 20 requisições simultâneas: excedente recusado com 503 |
| CT09 | descrição OpenAPI servida pelo dispositivo |
| CT10 | barramento livre sem requisições nem assinaturas |

## Dados

- `dados/2026-10-03-barramento-livre.json`: CT01-CT10 (exceto CT07) e 50
  amostras de tempo de resposta por operação, com o aplicativo fechado.
- `dados/2026-10-03-ct07.json`: escravo sem resposta e recuperação.

Os tempos são medidos no cliente, do envio da requisição ao fim da resposta,
com uma conexão TCP nova por requisição; o nome mDNS é resolvido uma única vez.
