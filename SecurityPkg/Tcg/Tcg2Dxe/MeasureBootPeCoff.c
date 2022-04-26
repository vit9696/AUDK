/** @file
  This module implements measuring PeCoff image for Tcg2 Protocol.

  Caution: This file requires additional review when modified.
  This driver will have external input - PE/COFF image.
  This external input must be validated carefully to avoid security issue like
  buffer overflow, integer overflow.

Copyright (c) 2015 - 2018, Intel Corporation. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <PiDxe.h>

#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DevicePathLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/PeCoffLib.h>
#include <Library/Tpm2CommandLib.h>
#include <Library/HashLib.h>

/**
  Measure PE image into TPM log based on the authenticode image hashing in
  PE/COFF Specification 8.0 Appendix A.

  Caution: This function may receive untrusted input.
  PE/COFF image is external input, so this function will validate its data structure
  within this image buffer before use.

  Notes: PE/COFF image is checked by BasePeCoffLib PeCoffInitializeContext().

  @param[in]  PCRIndex       TPM PCR index
  @param[in]  ImageAddress   Start address of image buffer.
  @param[in]  ImageSize      Image size
  @param[out] DigestList     Digest list of this image.

  @retval EFI_SUCCESS            Successfully measure image.
  @retval EFI_OUT_OF_RESOURCES   No enough resource to measure image.
  @retval other error value
**/
EFI_STATUS
MeasurePeImageAndExtend (
  IN  UINT32                PCRIndex,
  IN  EFI_PHYSICAL_ADDRESS  ImageAddress,
  IN  UINTN                 ImageSize,
  OUT TPML_DIGEST_VALUES    *DigestList
  )
{
  EFI_STATUS                           Status;
  EFI_IMAGE_SECTION_HEADER             *SectionHeader;
  HASH_HANDLE                          HashHandle;
  PE_COFF_LOADER_IMAGE_CONTEXT         ImageContext;

  HashHandle = 0xFFFFFFFF; // Know bad value

  Status        = EFI_UNSUPPORTED;
  SectionHeader = NULL;

  // FIXME: Can this somehow be abstracted away?
  //
  // Get information about the image being loaded
  //
  Status = PeCoffInitializeContext (
             &ImageContext,
             (VOID *) (UINTN) ImageAddress,
             (UINT32) ImageSize
             );
  if (EFI_ERROR (Status)) {
    //
    // The information can't be got from the invalid PeImage
    //
    DEBUG ((DEBUG_INFO, "Tcg2Dxe: PeImage invalid. Cannot retrieve image information.\n"));
    goto Finish;
  }

  //
  // PE/COFF Image Measurement
  //
  //    NOTE: The following codes/steps are based upon the authenticode image hashing in
  //      PE/COFF Specification 8.0 Appendix A.
  //
  //

  // Initialize a SHA hash context.

  Status = HashStart (&HashHandle);
  if (EFI_ERROR (Status)) {
    goto Finish;
  }

  //
  // Measuring PE/COFF Image Header;
  // But CheckSum field and SECURITY data directory (certificate) are excluded
  //

  // FIXME: This is just an ugly wrapper, the types should match (UINTN <-> VOID *), fix the libs
  PeCoffHashImageAuthenticode (NULL, (VOID *) HashHandle, (PE_COFF_LOADER_HASH_UPDATE) HashUpdate);
  if (EFI_ERROR (Status)) {
    goto Finish;
  }

  //
  // 17.  Finalize the SHA hash.
  //
  Status = HashCompleteAndExtend (HashHandle, PCRIndex, NULL, 0, DigestList);
  if (EFI_ERROR (Status)) {
    goto Finish;
  }

Finish:
  if (SectionHeader != NULL) {
    FreePool (SectionHeader);
  }

  return Status;
}
