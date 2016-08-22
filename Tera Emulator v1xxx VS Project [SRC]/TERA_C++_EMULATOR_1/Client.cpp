#include "Client.hpp"
#include "Server.hpp"
#include "Account.hpp"
#include "Player.hpp"
#include "PlayerService.h"
#include "Stream.h"
#include "TeraPacket.hpp"
#include "OpCodesEnum.h"
#include "OpCodes.hpp"
#include "SendPacket.h"

Client::Client(SOCKET socket, sockaddr_in sockData, Server * server) : Entity()
{
	std::cout << "Client connected!\n";
	_socket = socket;
	_sockData = sockData;
	_playerLocked = false;
	_run = false;
	_opened = true;
	_session = 0;
	_connectionId = -1;

	int val = 1;
	setsockopt(_socket, SOL_SOCKET, SO_KEEPALIVE, (const char*)&val, sizeof val);

	_clientThread = std::thread(Run, this, server);
	_clientThread.detach();
}
Client::~Client()
{
	if (_session)
	{
		delete _session;
		_session = 0;
	}
	_account = 0;
}

void Client::Close()
{
	_run = false;
	_opened = false;
	shutdown(_socket, SD_BOTH);
	closesocket(_socket);
}
void Client::SetId(int id)
{
	_connectionId = id;
}

void Client::Run(Client * instance, Server* server)
{

	int read = 0; bool err = true;
	srand(time((time_t)0));
	char
		clientKey1[128],
		clientKey2[128],
		serverKey1[128],
		serverKey2[128];

	char sInit[4];
	memset(sInit, 0, 4);
	sInit[0] = 1;

	memset(clientKey1, 0, 128); memset(clientKey2, 0, 128);
	for (int j = 0; j < 128; j++)
	{
		serverKey1[j] = rand() % 0xff;
		serverKey2[j] = rand() % 0xff;
	}

	read = send(instance->_socket, sInit, 4, 0);
	if (read != 0 && read != -1)
	{
		read = recv(instance->_socket, clientKey1, 128, 0);
		if (read != 0 && read != -1)
		{
			read = send(instance->_socket, serverKey1, 128, 0);
			if (read != 0 && read != -1)
			{
				read = recv(instance->_socket, clientKey2, 128, 0);
				if (read != 0 && read != -1)
				{
					read = send(instance->_socket, serverKey2, 128, 0);
					if (read != 0 && read != -1)
					{
						err = false;
					}
				}
			}
		}
	}

	if (!err)
	{
		instance->_canRecvVariable = true;
		instance->_run = true;
		instance->_session = new Crypt::Session((uint8_t*)clientKey1, (uint8_t*)clientKey2, (uint8_t*)serverKey1, (uint8_t*)serverKey2);
		Recevie(instance);
	}


	instance->_run = false;


	PlayerService::SaveAccountData(instance->_account);
	std::cout << "Client disconnected! EntityID[" << instance->_entityId << "] err?[" << err << "]\n";
	
	instance->_account->_loggedIn = false;
	server->EndConnection(instance);
}
void Client::Recevie(Client * instance)
{
	TeraPacket * _packet = new TeraPacket();
	Stream * processStream = new Stream();

	while (instance->_run)
	{
		if (!instance->_canRecvVariable)
			instance->_canRecv.wait(std::unique_lock<std::mutex>(instance->_recvMutex));


		if (!_packet->GetPacket(instance->_socket, instance->_session))
		{
			break;
		}

		instance->ProcessData(instance, _packet, processStream);
		_packet->Reset();
	}


	if (_packet)
	{
		delete _packet;
		_packet = 0;
	}

	processStream->Clear();
	delete processStream;
	processStream = 0;

}
const bool Client::ProcessData(Client *  instance, TeraPacket* packet, Stream * processStream)
{
	instance->Dump(packet->_raw, packet->_size, true);//logs hex text data to recvDump.txt
	//std::cout << ">" << ServerUtils::HexString(packet->_raw, packet->_size) << std::endl;//------------DEBUGs


	SendPacket* toSend = OpCodes::Get((packet->_opCode[0] << 8) | packet->_opCode[1]);
	if (!toSend)
	{
		std::cout << "No resolution was found for Client OpCode[" << ServerUtils::HexString(packet->_opCode, 2) << "] OpCodes Resolutions need to be updated\n";
		return true;
	}

	processStream->Clear();
	if (packet->_size > 4)
	{
		processStream->Write(&packet->_raw[4], packet->_size - 4);
		processStream->SetFront();
	}

	toSend->Process((OpCode)((packet->_opCode[0] << 8) | packet->_opCode[1]), processStream, instance);

	return true;
}

const bool Client::IsVisibleClient(Client * c)
{
	for (size_t i = 0; i < _visibleClients.size(); i++)
	{
		if (_visibleClients[i] == c)
			return true;
	}
	return false;
}
void Client::RemoveVisibleClient(Client * c)
{
	int j = -1;
	for (size_t i = 0; i < _visibleClients.size(); i++)
	{
		if (_visibleClients[i] == c)
		{
			j = i;
			break;
		}
	}

	if (j >= 0)
	{
		Stream s = Stream();
		s.WriteInt16(0);
		s.WriteInt16(S_DESPAWN_USER);

		s.WriteInt64(c->_account->_selectedPlayer->_entityId);
		s.WriteInt32(0); //unk

		s.WritePos(0);
		BroadcastSystem::Broadcast(this, &s, ME, 0);
		s.Clear();


		_visibleClients[j] = 0;
		_visibleClients.erase(_visibleClients.begin() + j);
	}
}
void Client::AddVisibleClient(Client * c)
{
	Player * p = _account->_selectedPlayer;
	Stream* data = new  Stream();
	data->WriteInt16(0);
	data->WriteInt16(S_SPAWN_USER);

	data->WriteInt64(0);



	short namePos = data->NextPos();
	short guildNamePos = data->NextPos();
	short Title = data->NextPos();

	short details1Pos = data->NextPos();
	data->WriteInt16(32);

	short gTitlePos = data->NextPos();
	short gTitleIconPos = data->NextPos();

	short details2Pos = data->NextPos();
	data->WriteInt16(64);

	data->WriteInt64(_account->_selectedPlayer->_entityId + 1);
	data->WriteInt64(_entityId + 1);

	data->WriteFloat(p->_position->_X + 10);
	data->WriteFloat(p->_position->_Y + 10);
	data->WriteFloat(p->_position->_Z + 10);
	data->WriteInt16(p->_position->_heading);

	data->WriteInt32(0); //relation ?? enemy / party member ...
	data->WriteInt32(p->_model);
	data->WriteInt16(0); //allawys 0?
	data->WriteInt16(0); //unk2
	data->WriteInt16(0); //unk3
	data->WriteInt16(0); //unk4 allways 0?
	data->WriteInt16(0); //unk5 0-3 ?

	data->WriteByte(1);
	data->WriteByte(1); //alive?

	data->Write(p->_data, 8);

	data->WriteInt32(p->_playerWarehouse->weapon);
	data->WriteInt32(p->_playerWarehouse->armor);
	data->WriteInt32(p->_playerWarehouse->gloves);
	data->WriteInt32(p->_playerWarehouse->boots);
	data->WriteInt32(p->_playerWarehouse->innerWare);
	data->WriteInt32(p->_playerWarehouse->skin1);
	data->WriteInt32(p->_playerWarehouse->skin2);

	data->WriteInt32(0); //unk 0-1-3 ??
	data->WriteInt32(0); //mount...
	data->WriteInt32(7); //7 ???
	data->WriteInt32(0); // Title id



	data->WriteInt64(0);

	data->WriteInt64(0);
	data->WriteInt64(0);

	data->WriteInt64(0);
	data->WriteInt64(0);

	data->WriteInt64(0);
	data->WriteInt64(0);

	data->WriteInt32(0);	  //unk s
	data->WriteInt16(0);
	data->WriteByte(0); //allaways 0?

	data->WriteByte(0);		  //enchants ??
	data->WriteByte(0);		  //enchants ??
	data->WriteByte(0);		  //enchants ??
	data->WriteByte(0);		  //enchants ??

	data->WriteByte(0);
	data->WriteByte(0);

	data->WriteInt16(p->_level);

	data->WriteInt16(0);   //always 0?
	data->WriteInt32(0);   //always 0?
	data->WriteInt32(0);
	data->WriteByte(0); //unk boolean?

	data->WriteInt32(0);	//skins ?
	data->WriteInt32(0);	//skins ?
	data->WriteInt32(0);	//skins ?
	data->WriteInt32(0);	//skins ?
	data->WriteInt32(0);	//skins ?
	data->WriteInt32(0); //costumeDye # ?

	data->WriteInt32(0);
	data->WriteInt32(0);

	data->WriteByte(0); //boolean?

	data->WriteInt32(0);
	data->WriteInt32(0);
	data->WriteInt32(0);
	data->WriteInt32(0);
	data->WriteInt32(0);

	data->WriteByte(1); //boolean?
	data->WriteInt32(0);

	data->WriteFloat(1.0f); //allways 1.0f?

	data->WritePos(namePos);
	data->WriteString(p->_name);

	data->WritePos(guildNamePos);
	data->WriteInt16(0);
	data->WritePos(Title);
	data->WriteInt16(0);

	data->WritePos(details1Pos);
	data->Write(p->_details1, 32);


	data->WritePos(gTitlePos);
	data->WriteInt16(0);
	data->WritePos(gTitleIconPos);
	data->WriteInt16(0);

	data->WritePos(details2Pos);
	data->Write(p->_details2, 64);

	data->WritePos(0); //size
	Send(data);
	data->Clear();

	delete data;
	data = 0;

	_visibleClients.push_back(c);
}

const bool Client::Send(byte * data, unsigned int length)
{
	Dump(data, length, false); //logs hex text data to sendDump.txt
	//std::cout << "<" << ServerUtils::HexString(data, length) << std::endl;//------------DEBUG

	_sendMutex.lock();

	_session->Encrypt(data, length);
	int ret = send(_socket, (const char*)data, length, 0);

	_sendMutex.unlock();

	if (ret == SOCKET_ERROR)
		return false;

	return true;
}

const bool Client::Send(Stream * s)
{
	return Send(s->_raw, s->_size);
}

void Client::Dump(byte* data, unsigned int length, bool recv)
{
	if (recv)
	{

		std::fstream f = std::fstream("C://users//narcis//desktop//recvDump.txt", std::ios::app);

		f << "Dump opcode:" << ServerUtils::HexString(&data[2], 2) << "]\n";
		f << ServerUtils::HexString(data, length);
		f << "\n\n";
		f.close();

	}
	else
	{
		std::fstream f = std::fstream("C://users//narcis//desktop//sendDump.txt", std::ios::app);

		f << "Dump opcode:" << ServerUtils::HexString(&data[2], 2) << "]\n";
		f << ServerUtils::HexString(data, length);
		f << "\n\n";
		f.close();

	}
}

Player * Client::LockPlayer()
{
	if (_playerLocked)
		return nullptr;
	_playerLocked = true;
	return _account->_selectedPlayer;
}
const bool Client::IsLocked()
{
	return _playerLocked;
}
void Client::UnlockPlayer()
{
	if (_playerLocked)
		_playerLocked = false;
}

