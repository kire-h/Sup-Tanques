#include <iostream>     /* cerr */
#include <algorithm>
#include "supservidor.h"

using namespace std;

/* ========================================
   CLASSE SUPSERVIDOR
   ======================================== */

/// Construtor
SupServidor::SupServidor()
  : Tanks()
  , server_on(false)
  , LU()
  , thr_server()
  , sock_server()
  /*ACRESCENTAR*/
{
  // Inicializa a biblioteca de sockets
  mysocket_status iResult = mysocket::init();
  /*ACRESCENTAR*/
  // Em caso de erro, mensagem e encerra
  if (iResult != mysocket_status::SOCK_OK)
  {
    cerr <<  "Biblioteca mysocket nao pode ser inicializada";
    exit(-1);
  }
}

/// Destrutor
SupServidor::~SupServidor()
{
  // Deve parar a thread do servidor
  server_on = false;

  // Fecha todos os sockets dos clientes
  for (auto& U : LU) U.close();
  // Fecha o socket de conexoes
  sock_server.close();
  /*ACRESCENTAR*/

  // Espera o fim da thread do servidor
  if (thr_server.joinable()) thr_server.join();
  /*ACRESCENTAR*/

  // Encerra a biblioteca de sockets
  mysocket::end();
  /*ACRESCENTAR*/
}

/// Liga o servidor
bool SupServidor::setServerOn()
{
  // Se jah estah ligado, nao faz nada
  if (server_on) return true;

  // Liga os tanques
  setTanksOn();

  // Indica que o servidor estah ligado a partir de agora
  server_on = true;

  try
  {
    // Coloca o socket de conexoes em escuta
    mysocket_status iResult = sock_server.listen(SUP_PORT);
    /*ACRESCENTAR*/
    // Em caso de erro, gera excecao
    if (iResult != mysocket_status::SOCK_OK) throw 1;

    // Lanca a thread do servidor que comunica com os clientes
    thr_server = thread( [this]()
    {
      this->thr_server_main();
    } );
    /*ACRESCENTAR*/
    // Em caso de erro, gera excecao
    if (!thr_server.joinable()) throw 2;
  }
  catch(int i)
  {
    cerr << "Erro " << i << " ao iniciar o servidor\n";

    // Deve parar a thread do servidor
    server_on = false;

    // Fecha o socket do servidor
    sock_server.close();

    return false;
  }

  // Tudo OK
  return true;
}

/// Desliga o servidor
void SupServidor::setServerOff()
{
  // Se jah estah desligado, nao faz nada
  if (!server_on) return;

  // Deve parar a thread do servidor
  server_on = false;

  // Fecha todos os sockets dos clientes
  for (auto& U : LU) U.close();
  // Fecha o socket de conexoes
  sock_server.close();
  /*ACRESCENTAR*/

  // Espera pelo fim da thread do servidor
  if (thr_server.joinable()) thr_server.join();
  /*ACRESCENTAR*/
  // Faz o identificador da thread apontar para thread vazia
  thr_server = thread();
  /*ACRESCENTAR*/

  // Desliga os tanques
  setTanksOff();
}

/// Leitura do estado dos tanques
void SupServidor::readStateFromSensors(SupState& S) const
{
  // Estados das valvulas: OPEN, CLOSED
  S.V1 = v1isOpen();
  S.V2 = v2isOpen();
  // Niveis dos tanques: 0 a 65535
  S.H1 = hTank1();
  S.H2 = hTank2();
  // Entrada da bomba: 0 a 65535
  S.PumpInput = pumpInput();
  // Vazao da bomba: 0 a 65535
  S.PumpFlow = pumpFlow();
  // Estah transbordando (true) ou nao (false)
  S.ovfl = isOverflowing();
}

/// Leitura e impressao em console do estado da planta
void SupServidor::readPrintState() const
{
  if (tanksOn())
  {
    SupState S;
    readStateFromSensors(S);
    S.print();
  }
  else
  {
    cout << "Tanques estao desligados!\n";
  }
}

/// Impressao em console dos usuarios do servidor
void SupServidor::printUsers() const
{
  for (const auto& U : LU)
  {
    cout << U.login << '\t'
         << "Admin=" << (U.isAdmin ? "SIM" : "NAO") << '\t'
         << "Conect=" << (U.isConnected() ? "SIM" : "NAO") << '\n';
  }
}

/// Adicionar um novo usuario
bool SupServidor::addUser(const string& Login, const string& Senha,
                             bool Admin)
{
  // Testa os dados do novo usuario
  if (Login.size()<6 || Login.size()>12) return false;
  if (Senha.size()<6 || Senha.size()>12) return false;

  // Testa se jah existe usuario com mesmo login
  auto itr = find(LU.begin(), LU.end(), Login);
  if (itr != LU.end()) return false;

  // Insere
  LU.push_back( User(Login,Senha,Admin) );

  // Insercao OK
  return true;
}

/// Remover um usuario
bool SupServidor::removeUser(const string& Login)
{
  // Testa se existe usuario com esse login
  auto itr = find(LU.begin(), LU.end(), Login);
  if (itr == LU.end()) return false;

  // Remove
  LU.erase(itr);

  // Remocao OK
  return true;
}

/// A thread que implementa o servidor.
/// Comunicacao com os clientes atraves dos sockets.
void SupServidor::thr_server_main(void)
{
  // Fila de sockets para aguardar chegada de dados
  /*ACRESCENTAR*/

  mysocket_queue f;

  tcp_mysocket t;

  uint16_t cmd;

  string login, senha;

  uint16_t estado;
  std::list<User>::iterator iU;
  mysocket_status iResult;
  SupState S;
  while (server_on)
  {
    // Erros mais graves que encerram o servidor
    // Parametro do throw e do catch eh uma const char* = "texto"
    try
    {
      // Encerra se o socket de conexoes estiver fechado
      if (!sock_server.accepting())
      {
        throw "socket de conexoes fechado";
      }

      // Inclui na fila de sockets todos os sockets que eu
      // quero monitorar para ver se houve chegada de dados

      // Limpa a fila de sockets
      f.clear();
      // Inclui na fila o socket de conexoes
      f.include(sock_server);
      // Inclui na fila todos os sockets dos clientes conectados
      for (auto& U : LU)
      {
        if (U.isConnected()) f.include(U.sock);
      }

      // Espera ateh que chegue dado em algum socket (com timeout)
      iResult = f.wait_read(SUP_TIMEOUT*1000);


      // De acordo com o resultado da espera:
      switch (iResult)
      {
      // SOCK_TIMEOUT:
      case mysocket_status::SOCK_TIMEOUT:
        // Saiu por timeout: nao houve atividade em nenhum socket
        // Aproveita para salvar dados ou entao nao faz nada
        break;
      // SOCK_ERROR:
      case mysocket_status::SOCK_ERROR:
          // Erro no select: encerra o servidor
      default:
        throw "Erro no select";
        break;
      // SOCK_OK:
      case mysocket_status::SOCK_OK:
        try
        {
          // Houve atividade em algum socket da fila:
          for (iU=LU.begin(); server_on && iU!=LU.end(); ++iU)
          {
            //   Testa se houve atividade nos sockets dos clientes. Se sim:
            if (server_on && iU->isConnected() && f.had_activity(iU->sock))
            {
                //   - Leh o comando
                iResult = iU->sock.read_uint16(cmd,SUP_TIMEOUT*1000);
                if (iResult != mysocket_status::SOCK_OK) throw 1;

                //   - Executa a acao
                switch(cmd)
              {
              case CMD_LOGIN:
              case CMD_ADMIN_OK:
              case CMD_OK:
              case CMD_ERROR:
              case CMD_DATA :
              default:
                throw 2;
                break;
              case CMD_GET_DATA:
                iU->sock.write_uint16(CMD_DATA);

                readStateFromSensors(S);
                iU->sock.write_uint16(S.V1);
                iU->sock.write_uint16(S.V2);
                iU->sock.write_uint16(S.H1);
                iU->sock.write_uint16(S.H2);
                iU->sock.write_uint16(S.PumpInput);
                iU->sock.write_uint16(S.PumpFlow);
                iU->sock.write_uint16(S.ovfl);

                break;
              case CMD_SET_V1:
                cout << "mudando valvula 1" << endl;
                if (!iU->isAdmin)  throw 3;
                iResult = iU->sock.read_uint16(estado,SUP_TIMEOUT*1000);
                if (iResult != mysocket_status::SOCK_OK) throw 4;
                setV1Open(estado != 0);
                iResult = iU->sock.write_uint16(CMD_OK);
                if (iResult != mysocket_status::SOCK_OK)throw 5;
                break;
              case CMD_SET_V2:
                cout << "mudando valvula 2" << endl;
                if (!iU->isAdmin)  throw 6;
                iResult = iU->sock.read_uint16(estado,SUP_TIMEOUT*1000);
                if (iResult != mysocket_status::SOCK_OK) throw 7;
                setV2Open(estado != 0);
                iResult = iU->sock.write_uint16(CMD_OK);
                if (iResult != mysocket_status::SOCK_OK)throw 8;
                break;
              case CMD_SET_PUMP :
                cout << "mudando bomba" << endl;
                if (!iU->isAdmin)  throw 9;
                iResult = iU->sock.read_uint16(estado,SUP_TIMEOUT*1000);
                if (iResult != mysocket_status::SOCK_OK) throw 10;
                setPumpInput(estado);
                iResult = iU->sock.write_uint16(CMD_OK);
                if (iResult != mysocket_status::SOCK_OK)throw 11;
                break;

              case CMD_LOGOUT:
                cout << iU->login << " desconectado"<< endl;
                iU->sock.write_uint16(CMD_OK);
                iU->close();
                break;
              }
            }
          }
        }
        catch (int erro)
        {
            if (iU->isConnected())
            {

                if (erro == 2 || erro == 3 || erro == 6 || erro == 9)
                {
                iU->sock.write_uint16(CMD_ERROR);
                }
            }


            if (erro == 1 || erro == 4 || erro == 5 ||erro == 7 || erro == 8 || erro == 10 || erro == 11)
            {
                iU->sock.close();
            }

            std::cerr << "Erro " << erro << " na leitura do usuário " << iU->login << '\n';
        }
        //   Depois, testa se houve atividade no socket de conexao. Se sim:
        if (server_on && sock_server.connected() && f.had_activity(sock_server))
        {
            //   - Estabelece nova conexao em socket temporario
            iResult = sock_server.accept(t);
            if (iResult != mysocket_status::SOCK_OK) throw "Falha na conexao temporaria";
            try
            {
                //   - Leh comando, login e senha
                iResult = t.read_uint16(cmd, SUP_TIMEOUT*1000);
                if (iResult != mysocket_status::SOCK_OK ||cmd!=CMD_LOGIN) throw 1;

                iResult = t.read_string(login, SUP_TIMEOUT*1000);
                if (iResult != mysocket_status::SOCK_OK) throw 2;

                iResult = t.read_string(senha, SUP_TIMEOUT*1000);
                if (iResult != mysocket_status::SOCK_OK) throw 3;
                 //   - Testa usuario
                 if (login.size()<6 || login.size()>12)throw 4;
                 if (senha.size()<6 || senha.size()>12) throw 4;
                 iU = find(LU.begin(), LU.end(), login);
                 if (iU == LU.end() || iU->password != senha || iU->isConnected()) throw 4;
                 //   - Se deu tudo certo, faz o socket temporario ser o novo socket
                 //     do cliente e envia confirmacao
                 iU->sock.swap(t);
                 iResult = iU->sock.write_uint16(iU->isAdmin ? CMD_ADMIN_OK : CMD_OK);
                 if (iResult != mysocket_status::SOCK_OK) throw 5;
                 cout << iU->login << " conectado" << endl;

            }
            catch(int erro)
            {
            cout << login << " tentou conectar" << endl;
            if (erro==4)
            {
              t.write_uint16(CMD_ERROR);
              t.close();
            }
            else
            {
              // Erro na comunicacao
              // Fecha o socket
              if (erro == 5)
              {
                iU->close();
              }
              else
              {

                t.close();
              }
              cerr << "Erro " << erro << " na conexao" << endl;
            }
          }

        }
        break;
      }

    } // fim try - Erros mais graves que encerram o servidor
    catch(const char* err)  // Erros mais graves que encerram o servidor
    {
      cerr << "Erro no servidor: " << err << endl;

      // Sai do while e encerra a thread
      server_on = false;

      // Fecha todos os sockets dos clientes
      for (auto& U : LU) U.close();
      // Fecha o socket de conexoes
      sock_server.close();  /*ACRESCENTAR*/

      // Os tanques continuam funcionando

    } // fim catch - Erros mais graves que encerram o servidor
  } // fim while (server_on)
}


