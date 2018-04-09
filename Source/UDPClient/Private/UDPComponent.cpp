
#include "UDPComponent.h"
#include "LambdaRunnable.h"
#include "Runtime/Sockets/Public/SocketSubsystem.h"

UUDPComponent::UUDPComponent(const FObjectInitializer &init) : UActorComponent(init)
{
	bShouldAutoConnect = true;
	bShouldAutoListen = true;
	bWantsInitializeComponent = true;
	bAutoActivate = true;
	SendIP = FString(TEXT("127.0.0.1"));
	SendPort = 3001;
	ReceivePort = 3002;
	SendSocketName = FString(TEXT("ue4-dgram-send"));
	ReceiveSocketName = FString(TEXT("ue4-dgram-receive"));

	BufferSize = 2 * 1024 * 1024;	//default roughly 2mb
}

void UUDPComponent::ConnectToSendSocket(const FString& InIP /*= TEXT("127.0.0.1")*/, const int32 InPort /*= 3000*/)
{
	RemoteAdress = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
	
	bool bIsValid;
	RemoteAdress->SetIp(*InIP, bIsValid);
	RemoteAdress->SetPort(InPort);

	if (!bIsValid)
	{
		UE_LOG(LogTemp, Error, TEXT("UDP address is invalid <%s:%d>"), *InIP, InPort);
		return ;
	}

	SenderSocket = FUdpSocketBuilder(*SendSocketName).AsReusable().WithBroadcast();

	//check(SenderSocket->GetSocketType() == SOCKTYPE_Datagram);

	//Set Send Buffer Size
	SenderSocket->SetSendBufferSize(BufferSize, BufferSize);
	SenderSocket->SetReceiveBufferSize(BufferSize, BufferSize);

	bool bDidConnect = SenderSocket->Connect(*RemoteAdress);

	if (bDidConnect)
	{
		//We should be ready to send things through our sending udp socket, no guarantees we're actually connected however.
		OnSendSocketConnected.Broadcast();
	}
	else
	{
		OnSendSocketConnectionProblem.Broadcast();
	}
}

void UUDPComponent::StartReceiveSocket(const int32 InListenPort /*= 3002*/)
{
	FIPv4Address Addr;
	FIPv4Address::Parse(TEXT("0.0.0.0"), Addr);

	//Create Socket
	FIPv4Endpoint Endpoint(Addr, InListenPort);

	ReceiverSocket = FUdpSocketBuilder(*ReceiveSocketName)
		.AsNonBlocking()
		.AsReusable()
		.BoundToEndpoint(Endpoint)
		.WithReceiveBufferSize(BufferSize);
	;

	FTimespan ThreadWaitTime = FTimespan::FromMilliseconds(100);
	UDPReceiver = new FUdpSocketReceiver(ReceiverSocket, ThreadWaitTime, TEXT("UDP RECEIVER"));
	UDPReceiver->OnDataReceived().BindLambda([this] (const FArrayReaderPtr& DataPtr, const FIPv4Endpoint& Endpoint){
		TArray<uint8> Data;
		Data.AddUninitialized(DataPtr->TotalSize());
		DataPtr->Serialize(Data.GetData(), DataPtr->TotalSize());
		
		OnMessage.Broadcast(Data);
	});
	OnReceiveSocketStarted.Broadcast();
}

void UUDPComponent::CloseReceiveSocket()
{
	if (ReceiverSocket)
	{
		OnReceiveSocketClosed.Broadcast();
	}
}

void UUDPComponent::CloseSendSocket()
{
	if (SenderSocket)
	{
		SenderSocket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(SenderSocket);
		SenderSocket = nullptr;

		//We disconnected on our end. Udp connections are by default unreliable.
		OnSendSocketDisconnected.Broadcast();
	}
}

void UUDPComponent::Emit(const TArray<uint8>& Bytes)
{
	if (SenderSocket->GetConnectionState() == SCS_Connected)
	{
		int32 BytesSent = 0;
		SenderSocket->Send(Bytes.GetData(), Bytes.Num(), BytesSent);
	}
}

void UUDPComponent::InitializeComponent()
{
	Super::InitializeComponent();
}

void UUDPComponent::UninitializeComponent()
{
	Super::UninitializeComponent();
}

void UUDPComponent::BeginPlay()
{
	Super::BeginPlay();

	if (bShouldAutoListen)
	{
		StartReceiveSocket(ReceivePort);
	}
	if (bShouldAutoConnect)
	{
		ConnectToSendSocket(SendIP, SendPort);
	}
}

void UUDPComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CloseSendSocket();
	CloseReceiveSocket();

	Super::EndPlay(EndPlayReason);
}
