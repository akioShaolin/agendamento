Instalar Codesys 3.5.x

Instalar plugins da Wago

Ligar o CC100 na energia elétrica e conectar a rede cabeada, a mesma do computador
Localizar IP e configurar a página devices

Acessar o IP para acessar o Web-based Management
Login e senha: admin / wago
Trocar senha para solar123

Atualizar firmware com o WAGOUpdate para a versão 6.2.x
Ache o IP do CC100, baixe o arquivo de firmware e faça o upgrade

Na hora de criar o código, utilize a sessão superior para declarar variáveis e a inferior para rodar o código

Habilitar o RTC no Web-based Management
Configuration --> Clock --> Clock Settings
Timezone: Custom Timezone
TZ String: America/Sao_Paulo
Acerte os horários e depois vá em:
Configuration --> Ports and Services --> NTP Client
NTP Client Configuration:
Service enabled
Update Interval (sec): 600
Time Server 1: ntp.br
Time Server 2: pool.ntp.org
Update Time
Submit