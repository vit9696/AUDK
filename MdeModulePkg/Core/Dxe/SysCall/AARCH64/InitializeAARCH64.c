/** @file

  Copyright (c) 2024 - 2025, Mikhail Krichanov. All rights reserved.
  SPDX-License-Identifier: BSD-3-Clause

**/

#include <Chipset/AArch64.h>
#include <Guid/EarlyPL011BaseAddress.h>
#include <Library/ArmLib.h>
#include <Library/ArmMmuLib.h>
#include <Library/DefaultExceptionHandlerLib.h>

#include "DxeMain.h"

UINTN  gUserPageTable;

STATIC UINTN  mConfigurationTable;
STATIC UINTN  mConfigurationTableSize;
STATIC UINTN  mUartBaseAddress;

EFI_STATUS
EFIAPI
ArmCallRing3 (
  IN RING3_CALL_DATA  *Data,
  IN UINTN            UserStackTop,
  IN VOID             *EntryPoint,
  IN UINTN            UserPageTable
  );

STATIC
EFI_STATUS
EFIAPI
SysCallBootService (
  IN EFI_SYSTEM_CONTEXT  Context
  )
{
  EFI_STATUS  Status;
  UINTN       *UserArguments;

  ArmEnableInterrupts ();

  UserArguments = (UINTN *)(Context.SystemContextAArch64->SP_EL0 - 7 * sizeof (UINTN));

  AllowSupervisorAccessToUserMemory ();
  //
  // First 6 arguments are passed through X2-X7 and copied to Core stack,
  // all the others are on User stack.
  //
  CopyMem (
    (VOID *)UserArguments,
    (VOID *)&(Context.SystemContextAArch64->X1),
    7 * sizeof (UINTN)
    );
  ForbidSupervisorAccessToUserMemory ();

  Status = CallBootService (
             Context.SystemContextAArch64->X0,
             Context.SystemContextAArch64->X1,
             UserArguments,
             Context.SystemContextAArch64->SP
             );

  ArmDisableInterrupts ();

  return Status;
}

EFI_STATUS
EFIAPI
MakeUserPageTableTemplate (
  OUT UINTN  *UserPageTableTemplate,
  OUT UINTN  *UserPageTableTemplateSize
  )
{
  ARM_MEMORY_REGION_DESCRIPTOR  Descriptor;
  VOID                          *MemorySizeHob;

  MemorySizeHob = GetFirstGuidHob (&gArmVirtSystemMemorySizeGuid);
  if (MemorySizeHob == NULL) {
    return EFI_NOT_FOUND;
  }

  Descriptor.PhysicalBase = PcdGet64 (PcdSystemMemoryBase);
  Descriptor.VirtualBase  = Descriptor.PhysicalBase;
  Descriptor.Length       = *(UINT64 *)GET_GUID_HOB_DATA (MemorySizeHob);
  Descriptor.Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK;

  return ArmMakeUserPageTableTemplate (
           &Descriptor,
           UserPageTableTemplate,
           UserPageTableTemplateSize
           );
}

EFI_STATUS
EFIAPI
InitializePlatform (
  IN OUT EFI_SYSTEM_TABLE  *System
  )
{
  EFI_STATUS                Status;
  UINTN                     Tcr;
  UINTN                     Sctlr;
  EFI_PHYSICAL_ADDRESS      Physical;
  UINTN                     Index;
  EFI_CONFIGURATION_TABLE   *Conf;
  EARLY_PL011_BASE_ADDRESS  *UartBase;
  CONST VOID                *Hob;

  mConfigurationTableSize = (System->NumberOfTableEntries + 1) * sizeof (EFI_CONFIGURATION_TABLE);

  Status = CoreAllocatePages (
             AllocateAnyPages,
             EfiRing3MemoryType,
             EFI_SIZE_TO_PAGES (mConfigurationTableSize),
             &Physical
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Conf = (EFI_CONFIGURATION_TABLE *)(UINTN)Physical;

  for (Index = 0; Index < System->NumberOfTableEntries; ++Index) {
    CopyGuid (&Conf->VendorGuid, &System->ConfigurationTable[Index].VendorGuid);

    Conf->VendorTable = System->ConfigurationTable[Index].VendorTable;

    ++Conf;
  }

  Hob              = GetFirstGuidHob (&gEarlyPL011BaseAddressGuid);
  UartBase         = GET_GUID_HOB_DATA (Hob);
  mUartBaseAddress = (UINTN)UartBase->DebugAddress;

  CopyGuid (&(Conf->VendorGuid), &gEarlyPL011BaseAddressGuid);
  Conf->VendorTable = (VOID *)mUartBaseAddress;
  ++System->NumberOfTableEntries;

  System->ConfigurationTable = (EFI_CONFIGURATION_TABLE *)(UINTN)Physical;
  mConfigurationTable        = (UINTN)Physical;

  //
  // Disable Hierarchical permissions just in case.
  //
  Tcr = ArmGetTCR ();
  Tcr |= TCR_EL1_HPD0_MASK | TCR_EL1_HPD1_MASK;
  ArmSetTCR (Tcr);

  if (ArmHasPan ()) {
    //
    // Enable Privileged Access Never feature.
    //
    Sctlr  = ArmReadSctlr ();
    Sctlr |= SCTLR_EPAN;
    ArmWriteSctlr (Sctlr);

    ArmSetPan ();
  }

  InitializeSysCallHandler ((VOID *)SysCallBootService);
  SetExceptionAddresses (NULL, 0);

  return EFI_SUCCESS;
}

VOID
EFIAPI
MapPlatform (
  IN OUT UINTN  UserPageTable
  )
{
  gCpu->SetUserMemoryAttributes (
          gCpu,
          UserPageTable,
          mConfigurationTable,
          ALIGN_VALUE (mConfigurationTableSize, EFI_PAGE_SIZE),
          EFI_MEMORY_XP | EFI_MEMORY_USER
          );
  //
  // Necessary fix for DEBUG printings.
  //
  gCpu->SetUserMemoryAttributes (
          gCpu,
          UserPageTable,
          mUartBaseAddress,
          SIZE_4KB,
          EFI_MEMORY_XP | EFI_MEMORY_USER
          );
}

VOID
EFIAPI
AllowSupervisorAccessToUserMemory (
  VOID
  )
{
  if (ArmHasPan ()) {
    ArmClearPan ();
  }
}

VOID
EFIAPI
ForbidSupervisorAccessToUserMemory (
  VOID
  )
{
  if (ArmHasPan ()) {
    ArmSetPan ();
  }
}

EFI_STATUS
EFIAPI
CallRing3 (
  IN RING3_CALL_DATA  *Data,
  IN UINTN            UserStackTop
  )
{
  return ArmCallRing3 (
            Data,
            UserStackTop,
            gRing3EntryPoint,
            gUserPageTable
            );
}