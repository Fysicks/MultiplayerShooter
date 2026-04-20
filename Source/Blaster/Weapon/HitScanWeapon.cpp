// Fill out your copyright notice in the Description page of Project Settings.

#include "HitScanWeapon.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Blaster/Character/BlasterCharacter.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystemComponent.h"
#include "Sound/SoundCue.h"
#include "DrawDebugHelpers.h"
#include "Kismet/KismetMathLibrary.h"
#include "WeaponTypes.h"

void AHitScanWeapon::Fire(const FVector& HitTarget) {
	Super::Fire(HitTarget);

	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (OwnerPawn == nullptr) return;
	AController* InstigatorController = OwnerPawn->GetController();
	if (!HasAuthority() && InstigatorController) {
		UE_LOG(LogTemp, Warning, TEXT("Instigator invalid"))
	}

	// Perform line trace
	const USkeletalMeshSocket* MuzzleFlashSocket = GetWeaponMesh()->GetSocketByName("MuzzleFlash");
	if (MuzzleFlashSocket) {
		FTransform SocketTransform = MuzzleFlashSocket->GetSocketTransform(GetWeaponMesh());
		FVector Start = SocketTransform.GetLocation();

		FHitResult FireHit;
		WeaponTraceHit(Start, HitTarget, FireHit);
		
		ABlasterCharacter* BlasterCharacter = Cast<ABlasterCharacter>(FireHit.GetActor());
		if (BlasterCharacter && HasAuthority() && InstigatorController) {
			UGameplayStatics::ApplyDamage(
				BlasterCharacter,
				Damage,
				InstigatorController,
				this,
				UDamageType::StaticClass()
			);
		}
		if (ImpactParticles) {
			UGameplayStatics::SpawnEmitterAtLocation(
				GetWorld(),
				ImpactParticles,
				FireHit.ImpactPoint,
				FireHit.ImpactNormal.Rotation()
			);
		}
		if (HitSound) {
			UGameplayStatics::PlaySoundAtLocation(
				this,
				HitSound,
				FireHit.ImpactPoint
			);
		}
		if (MuzzleFlash) {
			UGameplayStatics::SpawnEmitterAtLocation(
				GetWorld(),
				MuzzleFlash,
				SocketTransform
			);
			if (FireSound) {
				UGameplayStatics::PlaySoundAtLocation(
					this,
					FireSound,
					GetActorLocation()
				);
			}
		}
	}
}

void AHitScanWeapon::WeaponTraceHit(const FVector& TraceStart, const FVector& HitTarget, FHitResult& OutHit) {
	UWorld* World = GetWorld();
	if (World) {
		FVector End =  bUseScatter ?  TraceEndWithScatter(TraceStart, HitTarget) : TraceStart + (HitTarget - TraceStart) * 1.25; // End location just past hit target to ensure a hit target
		World->LineTraceSingleByChannel(
			OutHit,
			TraceStart,
			End,
			ECollisionChannel::ECC_Visibility
		);
		FVector BeamEnd = End; // Will get overwritten to blocking hit if there is one
		if (OutHit.bBlockingHit) {
			BeamEnd = OutHit.ImpactPoint;
			if (BeamParticles) {
				UParticleSystemComponent* Beam = UGameplayStatics::SpawnEmitterAtLocation(
					World,
					BeamParticles,
					TraceStart,
					FRotator::ZeroRotator,
					true
				);
				if (Beam) {
					Beam->SetVectorParameter(FName("Target"), BeamEnd);
				}
			}
		}
	}
}

FVector AHitScanWeapon::TraceEndWithScatter(const FVector& TraceStart, const FVector& HitTarget) {
	// Get a normalized vector that points from TraceStart to HitTarget
	FVector ToTargetNormalized = (HitTarget - TraceStart).GetSafeNormal();
	FVector SphereCenter = TraceStart + ToTargetNormalized * DistanceToSphere;
	// Getting random vector from the sphere center to a point within the radius
	FVector RandVect = UKismetMathLibrary::RandomUnitVector() * FMath::FRandRange(0.f, SphereRadius);
	FVector EndLoc = SphereCenter + RandVect;
	// Getting vector from trace start to that random point in the sphere
	FVector ToEndLoc = EndLoc - TraceStart;

	/**
	DrawDebugSphere(GetWorld(), SphereCenter, SphereRadius, 12, FColor::Red, true);
	DrawDebugSphere(GetWorld(), EndLoc, 4.f, 12, FColor::Blue, true);
	DrawDebugLine(GetWorld(), TraceStart, FVector(TraceStart + ToEndLoc * TRACE_LENGTH / ToEndLoc.Size()), FColor::Green, true);
	*/
	return FVector(TraceStart + ToEndLoc * TRACE_LENGTH / ToEndLoc.Size());
}


