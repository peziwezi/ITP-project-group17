// Copyright Epic Games, Inc. All Rights Reserved.

#include "ITPCharacter.h"
#include "Engine/LocalPlayer.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "Kismet/KismetMathLibrary.h"

DEFINE_LOG_CATEGORY(LogTemplateCharacter);

//////////////////////////////////////////////////////////////////////////
// AITPCharacter

AITPCharacter::AITPCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);
		
	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true; // Character moves in the direction of input...	
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f); // ...at this rotation rate

	// Note: For faster iteration times these variables, and many more, can be tweaked in the Character Blueprint
	// instead of recompiling to adjust them
	GetCharacterMovement()->JumpZVelocity = 700.f;
	GetCharacterMovement()->AirControl = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed = 500.f;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;

	// Create a camera boom (pulls in towards the player if there is a collision)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 400.0f; // The camera follows at this distance behind the character	
	CameraBoom->bUsePawnControlRotation = true; // Rotate the arm based on the controller

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName); // Attach the camera to the end of the boom and let the boom adjust to match the controller orientation
	FollowCamera->bUsePawnControlRotation = false; // Camera does not rotate relative to arm

	// Note: The skeletal mesh and anim blueprint references on the Mesh component (inherited from Character) 
	// are set in the derived blueprint asset named ThirdPersonCharacter (to avoid direct content references in C++)
}

void AITPCharacter::BeginPlay()
{
	// Call the base class  
	Super::BeginPlay();

	//Add Input Mapping Context
	if (APlayerController* PlayerController = Cast<APlayerController>(Controller))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(DefaultMappingContext, 0);
		}
	}
}

void AITPCharacter::Tick(float deltaSeconds)
{
	delta = deltaSeconds;

	DescendPlayer();
}

//////////////////////////////////////////////////////////////////////////
// Input

void AITPCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent)) {
		
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

		// Gliding
		EnhancedInputComponent->BindAction(GlideAction, ETriggerEvent::Started, this, &AITPCharacter::StartGliding);
		EnhancedInputComponent->BindAction(GlideAction, ETriggerEvent::Completed, this, &AITPCharacter::StopGliding);


		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AITPCharacter::Move);
	}
	else
	{
		UE_LOG(LogTemplateCharacter, Error, TEXT("'%s' Failed to find an Enhanced Input component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}

void AITPCharacter::Move(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D MovementVector = Value.Get<FVector2D>();

	if (Controller != nullptr)
	{
		// find out which way is forward
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

	
		// get right vector 
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		// add movement 
		AddMovementInput(RightDirection, MovementVector.X);
	}
}

void AITPCharacter::StartGliding()
{
	if (!bIsGliding && CanStartGliding()) {
		CurrentVelocity = GetCharacterMovement()->Velocity;
		bIsGliding = true;

		RecordOriginalSettings();

		GetCharacterMovement()->GravityScale = 0.0;
		GetCharacterMovement()->AirControl = 0.9;
		GetCharacterMovement()->BrakingDecelerationFalling = 350.f;
		GetCharacterMovement()->MaxAcceleration = 1024;
		GetCharacterMovement()->MaxWalkSpeed = 600;
	}
}


void AITPCharacter::StopGliding()
{
	ApplyOriginalSettings();
	bIsGliding = false;
}

bool AITPCharacter::CanStartGliding()
{
	FHitResult Hit;

	FVector TraceStart = GetActorLocation();
	FVector TraceEnd = GetActorLocation() + GetActorUpVector() * minimumHeight * -1.f;

	FCollisionQueryParams QueryParams;

	QueryParams.AddIgnoredActor(this);
	TEnumAsByte<ECollisionChannel> TraceProperties = ECC_Visibility;
	GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, TraceProperties, QueryParams);
	DrawDebugLine(GetWorld(), TraceStart, TraceEnd, Hit.bBlockingHit ? FColor::Blue : FColor::Red);

	if (!Hit.bBlockingHit && GetCharacterMovement()->IsFalling()) return true;

	return false;
}

void AITPCharacter::RecordOriginalSettings()
{
	originalGravityScale = GetCharacterMovement()->GravityScale;
	originalWalkingSpeed = GetCharacterMovement()->MaxWalkSpeed;
	originalDeceleration = GetCharacterMovement()->BrakingDecelerationFalling;
	originalAcceleration = GetCharacterMovement()->MaxAcceleration;
	originalAirControl = GetCharacterMovement()->AirControl;
}

void AITPCharacter::DescendPlayer()
{
	if (CurrentVelocity.Z != descendingRate * -1.f && bIsGliding) 
	{
		CurrentVelocity.Z = UKismetMathLibrary::FInterpEaseInOut(CurrentVelocity.Z, descendingRate, delta, 3.f);
		GetCharacterMovement()->Velocity.Z = descendingRate * -1.f;
	}
}

void AITPCharacter::ApplyOriginalSettings()
{
	GetCharacterMovement()->GravityScale = originalGravityScale;
	GetCharacterMovement()->MaxWalkSpeed = originalWalkingSpeed;
	GetCharacterMovement()->BrakingDecelerationFalling = originalDeceleration;
	GetCharacterMovement()->MaxAcceleration = originalAcceleration;
	GetCharacterMovement()->AirControl = originalAirControl;
}
