  <h1>Projeto Injeção Programada</h1>
  <p>
    O projeto <strong>Injeção Programada</strong> tem como objetivo controlar a potência injetada por inversores fotovoltaicos através de comandos enviados por um microcontrolador <strong>ESP8266</strong> via comunicação <strong>RS485</strong>.
  </p>

  <h2>Descrição Geral</h2>
  <p>
    O sistema foi desenvolvido para integrar-se a inversores fotovoltaicos, permitindo limitar a potência de injeção conforme parâmetros definidos pelo usuário ou condições externas, como controle de demanda, compensação de energia, ou restrições impostas pela concessionária.
  </p>

  <h2>Funcionalidades Implementadas</h2>
  <ul>
    <li>Comunicação entre o <strong>ESP8266</strong> e o inversor via <strong>RS485</strong>.</li>
    <li>Envio de comandos Modbus RTU para ajuste da potência ativa injetada.</li>
    <li>Leitura de registradores do inversor para monitoramento de status e potência.</li>
    <li>Estrutura modular do código em <code>Arduino</code>, com separação entre comunicação, controle e interface.</li>
    <li>Função de temporização para atualização periódica dos comandos.</li>
    <li>Tratamento de erros de comunicação e reconexão automática do barramento.</li>
  </ul>

  <h2>Tecnologias Utilizadas</h2>
  <ul>
    <li><strong>ESP8266</strong> — microcontrolador principal.</li>
    <li><strong>RS485</strong> — interface de comunicação industrial entre dispositivos.</li>
    <li><strong>Modbus RTU</strong> — protocolo de comunicação com o inversor.</li>
    <li><strong>Arduino IDE</strong> — ambiente de desenvolvimento.</li>
  </ul>

  <h2>Próximos Passos</h2>
  <ul>
    <li>Implementar controle dinâmico baseado em sensores externos (corrente, potência ou sinal da rede).</li>
    <li>Adicionar interface web para configuração remota via Wi-Fi.</li>
    <li>Registrar logs de operação e eventos de falha.</li>
  </ul>

  <p>
    Para mais detalhes técnicos e histórico de desenvolvimento, consulte os registros e discussões do projeto.
  </p>
