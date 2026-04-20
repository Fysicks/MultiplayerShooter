// Fill out your copyright notice in the Description page of Project Settings.


#include "BlasterCharacter.h"

/* Components */
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Particles/ParticleSystemComponent.h"

/* Input */
#include "EnhancedInputSubsystems.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

/* Widgets */
#include "Components/WidgetComponent.h"

/* Replicating Variables */
#include "Net/UnrealNetwork.h"

/* Blaster */
#include "Blaster/Weapon/Weapon.h"
#include "Blaster/BlasterComponents/CombatComponent.h"
#include "BlasterAnimInstance.h"
#include "Blaster/Blaster.h"
#include "Blaster/PlayerController/BlasterPlayerController.h"
#include "Blaster/GameMode/BlasterGameMode.h"

/* Gen */
#include "Kismet/KismetMathLibrary.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundCue.h"
#include "Blaster/PlayerState/BlasterPlayerState.h"
#include "Blaster/Weapon/WeaponTypes.h"

ABlasterCharacter::ABlasterCharacter()
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	SpawnCollisionHandlingMethod = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(GetMesh());
	CameraBoom->TargetArmLength = 600.f;
	CameraBoom->bUsePawnControlRotation = true;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false; // Because attached to boom

	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 0.f, 850.f); // <- How fast to orient to movement

	OverheadWidget = CreateDefaultSubobject<UWidgetComponent>(TEXT("OverheadWidget"));
	OverheadWidget->SetupAttachment(RootComponent);

	Combat = CreateDefaultSubobject<UCombatComponent>(TEXT("CombatComponent"));
	Combat->SetIsReplicated(true);

	// Manually setting that check box from the editor here
	GetCharacterMovement()->NavAgentProps.bCanCrouch = true;

	// Making it so characters don't affect view camera
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECollisionChannel::ECC_Camera, ECollisionResponse::ECR_Ignore);
	GetMesh()->SetCollisionObjectType(ECC_SkeletalMesh);
	GetMesh()->SetCollisionResponseToChannel(ECollisionChannel::ECC_Camera, ECollisionResponse::ECR_Ignore);
	GetMesh()->SetCollisionResponseToChannel(ECollisionChannel::ECC_Visibility, ECollisionResponse::ECR_Block);

	TurningInPlace = ETurningInPlace::ETIP_NotTurning;
	SetNetUpdateFrequency(66.f);
	SetMinNetUpdateFrequency(33.f);

	DissolveTimeline = CreateDefaultSubobject<UTimelineComponent>(TEXT("DissolveTimelineComponent"));
}

void ABlasterCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(ABlasterCharacter, OverlappingWeapon, COND_OwnerOnly);
	DOREPLIFETIME(ABlasterCharacter, Health);
	DOREPLIFETIME(ABlasterCharacter, bDisableGameplay);
}

void ABlasterCharacter::OnRep_ReplicatedMovement() {
	Super::OnRep_ReplicatedMovement();

	SimProxiesTurn();
	TimeSinceLastMovementReplication = 0.f;
}

void ABlasterCharacter::BeginPlay() {
	Super::BeginPlay();

	UpdateHUDHealth();

	if (HasAuthority()) {
		OnTakeAnyDamage.AddDynamic(this, &ABlasterCharacter::ReceiveDamage);
	}
}

void ABlasterCharacter::Tick(float DeltaTime) {
	Super::Tick(DeltaTime);

	RotateInPlace(DeltaTime);

	HideCameraIfCharacterClose();
	PollInit();
}

void ABlasterCharacter::RotateInPlace(float DeltaTime) {
	if (bDisableGameplay) { 
		bUseControllerRotationYaw = false;
		TurningInPlace = ETurningInPlace::ETIP_NotTurning;
		return; 
	}
	if (GetLocalRole() > ENetRole::ROLE_SimulatedProxy && IsLocallyControlled()) {
		AimOffset(DeltaTime);
	} else {
		TimeSinceLastMovementReplication += DeltaTime;
		if (TimeSinceLastMovementReplication > 0.25f) {
			OnRep_ReplicatedMovement();
		}
		CalculateAO_Pitch();
	}
}

void ABlasterCharacter::PollInit() {
	if (BlasterPlayerState == nullptr) {
		BlasterPlayerState = GetPlayerState<ABlasterPlayerState>();
		if (BlasterPlayerState) {
			BlasterPlayerState->AddToScore(0.f);
			BlasterPlayerState->AddToDefeats(0);
		}
	}
}

void ABlasterCharacter::PostInitializeComponents() {
	Super::PostInitializeComponents();

	if (Combat) {
		Combat->Character = this;
		Combat->PrimaryComponentTick.bCanEverTick = true; // Necessary to get CombatComponent to tick
	}
}

void ABlasterCharacter::Destroyed() {
	Super::Destroyed();

	if (ElimBotComponent) {
		ElimBotComponent->DestroyComponent();
	}

	ABlasterGameMode* BlasterGameMode = Cast<ABlasterGameMode>(UGameplayStatics::GetGameMode(this));
	bool bMatchNotInProgress = BlasterGameMode && BlasterGameMode->GetMatchState() != MatchState::InProgress;
	if (Combat && Combat->EquippedWeapon && bMatchNotInProgress) {
		Combat->EquippedWeapon->Destroy();
	}
}

/**
*	Movement
*/
void ABlasterCharacter::Move(const FInputActionValue& Value) {
	if (bDisableGameplay) { return; }
	const FVector2D MovementVector = Value.Get<FVector2D>();

	const FRotator Rotation = GetController()->GetControlRotation();
	const FRotator YawRotation(0.f, Rotation.Yaw, 0.f);

	const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	AddMovementInput(ForwardDirection, MovementVector.Y);

	const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
	AddMovementInput(RightDirection, MovementVector.X);
}

void ABlasterCharacter::Look(const FInputActionValue& Value) {
	const FVector2D LookAxisValue = Value.Get<FVector2D>();

	if (GetController()) {
		AddControllerYawInput(LookAxisValue.X);
		AddControllerPitchInput(LookAxisValue.Y);
	}
}

void ABlasterCharacter::Jump() {
	if (bDisableGameplay) { return; }
	if (bIsCrouched) {
		UnCrouch();
	} else {
		Super::Jump();
	}
}

void ABlasterCharacter::AimOffset(float DeltaTime) {
	if (Combat && Combat->EquippedWeapon == nullptr) return;
	float Speed = CalculateSpeed();
	bool bIsInAir = GetCharacterMovement()->IsFalling();

	if (Speed == 0.f && !bIsInAir) {
		// Then we are standing still and not jumpin
		bRotateRootBone = true;
		FRotator CurrentAimRotation = FRotator(0.f, GetBaseAimRotation().Yaw, 0.f);
		FRotator DeltaAimRotation = UKismetMathLibrary::NormalizedDeltaRotator(CurrentAimRotation, StartingAimRotation);
		AO_Yaw = DeltaAimRotation.Yaw;
		if (TurningInPlace == ETurningInPlace::ETIP_NotTurning) {
			InterpAO_Yaw = AO_Yaw;
		}
		bUseControllerRotationYaw = true;
		TurnInPlace(DeltaTime);
	}
	if (Speed > 0.f || bIsInAir) {
		// Running or jumping resets StartingAimRotation. Use as a refernce to find Delta in yaw
		// for when we're not moving but still looking around
		bRotateRootBone = false;
		StartingAimRotation = FRotator(0.f, GetBaseAimRotation().Yaw, 0.f);
		AO_Yaw = 0.f;
		bUseControllerRotationYaw = true;
		TurningInPlace = ETurningInPlace::ETIP_NotTurning;
	}
	CalculateAO_Pitch();
}

void ABlasterCharacter::CalculateAO_Pitch() {
	AO_Pitch = GetBaseAimRotation().Pitch;
	if (AO_Pitch > 90.f && !IsLocallyControlled()) {
		// This problem does not exist on local players, only on players that
		// are simulated(?) (Simulated Proxy I think is what they're called)

		// Map pitch from [270, 360) to [-90, 0)
		FVector2D InRange(270.f, 360.f);
		FVector2D OutRange(-90.f, 0.f);
		AO_Pitch = FMath::GetMappedRangeValueClamped(InRange, OutRange, AO_Pitch);
	}
}

void ABlasterCharacter::SimProxiesTurn() {
	if (Combat == nullptr || Combat->EquippedWeapon == nullptr) { return; }
	bRotateRootBone = false;
	float Speed = CalculateSpeed();
	if (Speed > 0.f) {
		TurningInPlace = ETurningInPlace::ETIP_NotTurning;
		return;
	}
	ProxyRotationLastFrame = ProxyRotation;
	ProxyRotation = GetActorRotation();
	ProxyYaw = UKismetMathLibrary::NormalizedDeltaRotator(ProxyRotation, ProxyRotationLastFrame).Yaw;

	if (FMath::Abs(ProxyYaw) > TurnThreshold) {
		if (ProxyYaw > TurnThreshold) {
			TurningInPlace = ETurningInPlace::ETIP_Right;
		} else if (ProxyYaw < -TurnThreshold) {
			TurningInPlace = ETurningInPlace::ETIP_Left;
		} else {
			TurningInPlace = ETurningInPlace::ETIP_NotTurning;
		}
		return;
	}
	TurningInPlace = ETurningInPlace::ETIP_NotTurning;
}

void ABlasterCharacter::TurnInPlace(float DeltaTime) {
	if (AO_Yaw > 90.f) {
		TurningInPlace = ETurningInPlace::ETIP_Right;
	} else if (AO_Yaw < -90.f) {
		TurningInPlace = ETurningInPlace::ETIP_Left;
	}
	if (TurningInPlace != ETurningInPlace::ETIP_NotTurning) {		  // \/ How fast we want to turn
		InterpAO_Yaw = FMath::FInterpTo(InterpAO_Yaw, 0.f, DeltaTime, 4.f);
		AO_Yaw = InterpAO_Yaw;
		if (FMath::Abs(AO_Yaw) < 15.f) {
			TurningInPlace = ETurningInPlace::ETIP_NotTurning;
			// Resetting starting aim rotation
			StartingAimRotation = FRotator(0.f, GetBaseAimRotation().Yaw, 0.f);
		}
	}
}

float ABlasterCharacter::CalculateSpeed() {
	FVector Velocity = GetVelocity();
	Velocity.Z = 0.f;
	return Velocity.Size();
}

void ABlasterCharacter::HideCameraIfCharacterClose() {
	if (!IsLocallyControlled()) { return; }
	if ((FollowCamera->GetComponentLocation() - GetActorLocation()).Size() < CameraThreshold) {
		GetMesh()->SetVisibility(false);
		if (Combat && Combat->EquippedWeapon && Combat->EquippedWeapon->GetWeaponMesh()) {
			Combat->EquippedWeapon->GetWeaponMesh()->bOwnerNoSee = true;
		}
	} else {
		GetMesh()->SetVisibility(true);
		if (Combat && Combat->EquippedWeapon && Combat->EquippedWeapon->GetWeaponMesh()) {
			Combat->EquippedWeapon->GetWeaponMesh()->bOwnerNoSee = false;
		}
	}
}

/**
*	Input
*/
void ABlasterCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) {
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* EnhancedInputComponent = CastChecked<UEnhancedInputComponent>(PlayerInputComponent)) {
		EnhancedInputComponent->BindAction(MovementAction, ETriggerEvent::Triggered, this, &ABlasterCharacter::Move);
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &ABlasterCharacter::Look);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Triggered, this, &ABlasterCharacter::Jump);
		EnhancedInputComponent->BindAction(EquipAction, ETriggerEvent::Triggered, this, &ABlasterCharacter::EquipButtonPressed);
		EnhancedInputComponent->BindAction(CrouchAction, ETriggerEvent::Triggered, this, &ABlasterCharacter::CrouchButtonPressed);
		EnhancedInputComponent->BindAction(AimAction, ETriggerEvent::Triggered, this, &ABlasterCharacter::AimButtonPressed);
		EnhancedInputComponent->BindAction(AimAction, ETriggerEvent::Completed, this, &ABlasterCharacter::AimButtonReleased);
		EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Started, this, &ABlasterCharacter::FireButtonPressed);
		EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Completed, this, &ABlasterCharacter::FireButtonReleased);
		EnhancedInputComponent->BindAction(ReloadAction, ETriggerEvent::Triggered, this, &ABlasterCharacter::ReloadButtonPressed);
	}
}

void ABlasterCharacter::EquipButtonPressed() {
	if (bDisableGameplay) { return; }
	if (Combat) {
		if (HasAuthority()) {
			Combat->EquipWeapon(OverlappingWeapon);
		} else {
			ServerEquipButtonPressed();
		}
	}
}

void ABlasterCharacter::CrouchButtonPressed() {
	if (bDisableGameplay) { return; }
	if (bIsCrouched) {
		UnCrouch();
	} else {
		Crouch();
	}
}

void ABlasterCharacter::AimButtonPressed() {
	if (bDisableGameplay) { return; }
	if (Combat) {
		Combat->SetAiming(true);
	}
}

void ABlasterCharacter::AimButtonReleased() {
	//if (bDisableGameplay) { return; }
	if (Combat) {
		Combat->SetAiming(false);
	}
}

void ABlasterCharacter::FireButtonPressed() {
	if (bDisableGameplay) { return; }
	if (Combat) {
		Combat->FireButtonPressed(true);
	}
}

void ABlasterCharacter::FireButtonReleased() {
	if (bDisableGameplay) { return; }
	if (Combat) {
		Combat->FireButtonPressed(false);
	}
}

void ABlasterCharacter::ReloadButtonPressed() {
	if (bDisableGameplay) { return; }
	if (Combat) {
		Combat->Reload();
	}
}

void ABlasterCharacter::ServerEquipButtonPressed_Implementation() {
	if (Combat) {
		Combat->EquipWeapon(OverlappingWeapon);
	}
}

/**
*	Montages
*/

void ABlasterCharacter::PlayMontage(UAnimMontage* Montage, FName SectionName) {
	if (Combat == nullptr || (Combat->EquippedWeapon == nullptr && !bElimmed)) { 
		return; 
	}

	UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
	if (AnimInstance) {
		AnimInstance->Montage_Play(Montage);
		if (SectionName != NAME_None) {
			AnimInstance->Montage_JumpToSection(SectionName);
		}
	}
	
}

void ABlasterCharacter::PlayFireMontage(bool bAiming) {
	if (FireWeaponMontage){
		PlayMontage(FireWeaponMontage, bAiming ? FName("RifleAim") : FName("RifleHip"));
	}
}

void ABlasterCharacter::PlayReloadMontage() {
	if (ReloadMontage) {
		FName SectionName;
		switch (Combat->EquippedWeapon->GetWeaponType()) {
		case EWeaponType::EWT_AssaultRifle:
			SectionName = FName("Rifle");
			break;
		case EWeaponType::EWT_RocketLauncher:
			SectionName = FName("Rifle");
			break;
		case EWeaponType::EWT_Pistol:
			SectionName = FName("Rifle");
			break;
		case EWeaponType::EWT_SMG:
			SectionName = FName("Rifle");
			break;
		}
		PlayMontage(ReloadMontage, SectionName);
	}
}

void ABlasterCharacter::PlayHitReactMontage() {
	if (HitReactMontage) {
		PlayMontage(HitReactMontage, FName("FromFront"));
	}
}

void ABlasterCharacter::PlayElimMontage() {
	if (HitReactMontage) {
		PlayMontage(ElimMontage, NAME_None);
	}
}

/**
*	Combat
*/

void ABlasterCharacter::UpdateHUDHealth() {
	BlasterPlayerController = BlasterPlayerController == nullptr ? Cast<ABlasterPlayerController>(Controller) : BlasterPlayerController;
	if (BlasterPlayerController) {
		BlasterPlayerController->SetHUDHealth(Health, MaxHealth);
	}
}

void ABlasterCharacter::UpdateDissolveMaterial(float DissolveValue) {
	if (DynamicDissolveMaterialInstance) {
		DynamicDissolveMaterialInstance->SetScalarParameterValue(TEXT("Dissolve"), DissolveValue);
	}
}

void ABlasterCharacter::StartDissolve() {
	DissolveTrack.BindDynamic(this, &ABlasterCharacter::UpdateDissolveMaterial);
	if (DissolveCurve && DissolveTimeline) {
		DissolveTimeline->AddInterpFloat(DissolveCurve, DissolveTrack);
		DissolveTimeline->Play();
	}
}

void ABlasterCharacter::Elim() {
	if (Combat && Combat->EquippedWeapon) {
		Combat->EquippedWeapon->Dropped();
	}
	MulticastElim();
	GetWorldTimerManager().SetTimer(
		ElimTimer,
		this,
		&ABlasterCharacter::ElimTimerFinished,
		ElimDelay
	);
}

void ABlasterCharacter::MulticastElim_Implementation() {
	if (BlasterPlayerController) {
		BlasterPlayerController->SetHUDWeaponAmmo(0);
	}
	bElimmed = true;
	PlayElimMontage();

	// Start dissolve effect
	if (DissolveMaterialInstance) {
		DynamicDissolveMaterialInstance = UMaterialInstanceDynamic::Create(DissolveMaterialInstance, this);
		GetMesh()->SetMaterial(0, DynamicDissolveMaterialInstance);
		DynamicDissolveMaterialInstance->SetScalarParameterValue(TEXT("Dissolve"), 0.55f);
		DynamicDissolveMaterialInstance->SetScalarParameterValue(TEXT("Glow"), 200.f);
	}
	StartDissolve();

	// Disable character movement
	bDisableGameplay = true;
	GetCharacterMovement()->DisableMovement(); // Stops dead char from falling through floor
	if (Combat) {
		Combat->FireButtonPressed(false);
	}

	// Disable collision
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Spawn elim bot
	if (ElimBotEffect) {
		FVector ElimBotSpawnPoint(GetActorLocation().X, GetActorLocation().Y, GetActorLocation().Z + 200.f);
		ElimBotComponent = UGameplayStatics::SpawnEmitterAtLocation(
			GetWorld(),
			ElimBotEffect,
			ElimBotSpawnPoint,
			GetActorRotation()
		);
	}
	if (ElimBotSound) {
		UGameplayStatics::SpawnSoundAtLocation(
			this,
			ElimBotSound,
			GetActorLocation()
		);
	}
}

void ABlasterCharacter::ElimTimerFinished() {
	ABlasterGameMode* BlasterGameMode = GetWorld()->GetAuthGameMode<ABlasterGameMode>();
	if (BlasterGameMode) {
		BlasterGameMode->RequestRespawn(this, Controller);
	}
}

void ABlasterCharacter::OnRep_Health() {
	UpdateHUDHealth();
	if (!bElimmed) {
		PlayHitReactMontage();
	}
}

void ABlasterCharacter::ReceiveDamage(AActor* DamagedActor, float Damage, const UDamageType* DamageType, AController* InstigatorController, AActor* DamageCauser) {
	Health = FMath::Clamp(Health - Damage, 0.f, MaxHealth);
	UpdateHUDHealth();
	PlayHitReactMontage();

	if (Health == 0.f) {
		ABlasterGameMode* BlasterGameMode = GetWorld()->GetAuthGameMode<ABlasterGameMode>();
		if (BlasterGameMode) {
			BlasterPlayerController = BlasterPlayerController == nullptr ? Cast<ABlasterPlayerController>(Controller) : BlasterPlayerController;
			ABlasterPlayerController* AttackerController = Cast<ABlasterPlayerController>(InstigatorController);
			BlasterGameMode->PlayerEliminated(this, BlasterPlayerController, AttackerController);
		}
	}
}

void ABlasterCharacter::SetOverlappingWeapon(AWeapon* Weapon) {
	if (OverlappingWeapon) {
		if (OverlappingWeapon) {
			OverlappingWeapon->ShowPickupWidget(false);
		}
	}
	OverlappingWeapon = Weapon;
	if (IsLocallyControlled()) {
		// Then we are on the character who is hosting the game
		if (OverlappingWeapon) {
			OverlappingWeapon->ShowPickupWidget(true);
		}
	}
}

void ABlasterCharacter::OnRep_OverlappingWeapon(AWeapon* LastWeapon) {
	if (OverlappingWeapon) {
		OverlappingWeapon->ShowPickupWidget(true);
	}

	if (LastWeapon) {
		LastWeapon->ShowPickupWidget(false);
	}
}

bool ABlasterCharacter::IsWeaponEquipped() {
	return (Combat && Combat->EquippedWeapon);
}

bool ABlasterCharacter::IsAiming() {
	return (Combat && Combat->bAiming);
}

AWeapon* ABlasterCharacter::GetEquippedWeapon() {
	if (Combat == nullptr) { return nullptr; }
	return Combat->EquippedWeapon;
}

FVector ABlasterCharacter::GetHitTarget() const {
	if (Combat == nullptr) { return FVector(); }
	return Combat->HitTarget;
}

ECombatState ABlasterCharacter::GetCombatState() const {
	if (Combat == nullptr) { return ECombatState::ECS_MAX; }
	return Combat->CombatState;
}
