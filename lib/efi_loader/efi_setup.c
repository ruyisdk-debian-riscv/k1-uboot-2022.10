// SPDX-License-Identifier: GPL-2.0+
/*
 *  EFI setup code
 *
 *  Copyright (c) 2016-2018 Alexander Graf et al.
 */

#define LOG_CATEGORY LOGC_EFI

#include <common.h>
#include <efi_loader.h>
#include <efi_variable.h>
#include <log.h>

#define OBJ_LIST_NOT_INITIALIZED 1

efi_status_t efi_obj_list_initialized = OBJ_LIST_NOT_INITIALIZED;

/*
 * Allow unaligned memory access.
 *
 * This routine is overridden by architectures providing this feature.
 */
void __weak allow_unaligned(void)
{
}

/**
 * efi_init_platform_lang() - define supported languages
 *
 * Set the PlatformLangCodes and PlatformLang variables.
 *
 * Return:	status code
 */
static efi_status_t efi_init_platform_lang(void)
{
	efi_status_t ret;
	efi_uintn_t data_size = 0;
	char *lang = CONFIG_EFI_PLATFORM_LANG_CODES;
	char *pos;

	/*
	 * Variable PlatformLangCodes defines the language codes that the
	 * machine can support.
	 */
	ret = efi_set_variable_int(u"PlatformLangCodes",
				   &efi_global_variable_guid,
				   EFI_VARIABLE_BOOTSERVICE_ACCESS |
				   EFI_VARIABLE_RUNTIME_ACCESS |
				   EFI_VARIABLE_READ_ONLY,
				   sizeof(CONFIG_EFI_PLATFORM_LANG_CODES),
				   CONFIG_EFI_PLATFORM_LANG_CODES, false);
	if (ret != EFI_SUCCESS)
		goto out;

	/*
	 * Variable PlatformLang defines the language that the machine has been
	 * configured for.
	 */
	ret = efi_get_variable_int(u"PlatformLang",
				   &efi_global_variable_guid,
				   NULL, &data_size, &pos, NULL);
	if (ret == EFI_BUFFER_TOO_SMALL) {
		/* The variable is already set. Do not change it. */
		ret = EFI_SUCCESS;
		goto out;
	}

	/*
	 * The list of supported languages is semicolon separated. Use the first
	 * language to initialize PlatformLang.
	 */
	pos = strchr(lang, ';');
	if (pos)
		*pos = 0;

	ret = efi_set_variable_int(u"PlatformLang",
				   &efi_global_variable_guid,
				   EFI_VARIABLE_NON_VOLATILE |
				   EFI_VARIABLE_BOOTSERVICE_ACCESS |
				   EFI_VARIABLE_RUNTIME_ACCESS,
				   1 + strlen(lang), lang, false);
out:
	if (ret != EFI_SUCCESS)
		printf("EFI: cannot initialize platform language settings\n");
	return ret;
}

#ifdef CONFIG_EFI_SECURE_BOOT
/**
 * efi_init_secure_boot - initialize secure boot state
 *
 * Return:	status code
 */
static efi_status_t efi_init_secure_boot(void)
{
	efi_guid_t signature_types[] = {
		EFI_CERT_SHA256_GUID,
		EFI_CERT_X509_GUID,
	};
	efi_status_t ret;

	ret = efi_set_variable_int(u"SignatureSupport",
				   &efi_global_variable_guid,
				   EFI_VARIABLE_READ_ONLY |
				   EFI_VARIABLE_BOOTSERVICE_ACCESS |
				   EFI_VARIABLE_RUNTIME_ACCESS,
				   sizeof(signature_types),
				   &signature_types, false);
	if (ret != EFI_SUCCESS)
		printf("EFI: cannot initialize SignatureSupport variable\n");

	return ret;
}
#else
static efi_status_t efi_init_secure_boot(void)
{
	return EFI_SUCCESS;
}
#endif /* CONFIG_EFI_SECURE_BOOT */

/**
 * efi_init_capsule - initialize capsule update state
 *
 * Return:	status code
 */
static efi_status_t efi_init_capsule(void)
{
	efi_status_t ret = EFI_SUCCESS;

	if (IS_ENABLED(CONFIG_EFI_HAVE_CAPSULE_UPDATE)) {
		ret = efi_set_variable_int(u"CapsuleMax",
					   &efi_guid_capsule_report,
					   EFI_VARIABLE_READ_ONLY |
					   EFI_VARIABLE_BOOTSERVICE_ACCESS |
					   EFI_VARIABLE_RUNTIME_ACCESS,
					   22, u"CapsuleFFFF", false);
		if (ret != EFI_SUCCESS)
			printf("EFI: cannot initialize CapsuleMax variable\n");
	}

	return ret;
}

/**
 * efi_init_os_indications() - indicate supported features for OS requests
 *
 * Set the OsIndicationsSupported variable.
 *
 * Return:	status code
 */
static efi_status_t efi_init_os_indications(void)
{
	u64 os_indications_supported = 0;

	if (IS_ENABLED(CONFIG_EFI_HAVE_CAPSULE_SUPPORT))
		os_indications_supported |=
			EFI_OS_INDICATIONS_CAPSULE_RESULT_VAR_SUPPORTED;

	if (IS_ENABLED(CONFIG_EFI_CAPSULE_ON_DISK))
		os_indications_supported |=
			EFI_OS_INDICATIONS_FILE_CAPSULE_DELIVERY_SUPPORTED;

	if (IS_ENABLED(CONFIG_EFI_CAPSULE_FIRMWARE_MANAGEMENT))
		os_indications_supported |=
			EFI_OS_INDICATIONS_FMP_CAPSULE_SUPPORTED;

	return efi_set_variable_int(u"OsIndicationsSupported",
				    &efi_global_variable_guid,
				    EFI_VARIABLE_BOOTSERVICE_ACCESS |
				    EFI_VARIABLE_RUNTIME_ACCESS |
				    EFI_VARIABLE_READ_ONLY,
				    sizeof(os_indications_supported),
				    &os_indications_supported, false);
}

/**
 * __efi_init_early() - handle initialization at early stage
 *
 * This function is called in efi_init_obj_list() only if
 * !CONFIG_EFI_SETUP_EARLY.
 *
 * Return:	status code
 */
static efi_status_t __efi_init_early(void)
{
	efi_status_t ret = EFI_SUCCESS;

	/* Allow unaligned memory access */
	allow_unaligned();

	/* Initialize root node */
	ret = efi_root_node_register();
	if (ret != EFI_SUCCESS)
		goto out;

	ret = efi_console_register();
	if (ret != EFI_SUCCESS)
		goto out;

	ret = efi_disk_init();
out:
	return ret;
}

/**
 * efi_init_early() - handle initialization at early stage
 *
 * external version of __efi_init_early(); expected to be called in
 * board_init_r().
 *
 * Return:	status code
 */
int efi_init_early(void)
{
	efi_status_t ret;

	ret = __efi_init_early();
	if (ret != EFI_SUCCESS) {
		/* never re-init UEFI subsystem */
		efi_obj_list_initialized = ret;
		return -1;
	}
	return 0;
}

/**
 * efi_init_obj_list() - Initialize and populate EFI object list
 *
 * Return:	status code
 */
efi_status_t efi_init_obj_list(void)
{
	efi_status_t ret = EFI_SUCCESS;

	/* Initialize once only */
	if (efi_obj_list_initialized != OBJ_LIST_NOT_INITIALIZED)
		return efi_obj_list_initialized;

	if (!IS_ENABLED(CONFIG_EFI_SETUP_EARLY)) {
		ret = __efi_init_early();
		if (ret != EFI_SUCCESS)
			goto out;
	}

	/* Set up console modes */
	efi_setup_console_size();

	/*
	 * Probe block devices to find the ESP.
	 * efi_disks_register() must be called before efi_init_variables().
	 */
	ret = efi_disks_register();
	if (ret != EFI_SUCCESS)
		goto out;

	/* Initialize variable services */
	ret = efi_init_variables();
	if (ret != EFI_SUCCESS)
		goto out;

	/* Define supported languages */
	ret = efi_init_platform_lang();
	if (ret != EFI_SUCCESS)
		goto out;

	/* Indicate supported features */
	ret = efi_init_os_indications();
	if (ret != EFI_SUCCESS)
		goto out;

	/* Initialize system table */
	ret = efi_initialize_system_table();
	if (ret != EFI_SUCCESS)
		goto out;

	if (IS_ENABLED(CONFIG_EFI_ECPT)) {
		ret = efi_ecpt_register();
		if (ret != EFI_SUCCESS)
			goto out;
	}

	if (IS_ENABLED(CONFIG_EFI_ESRT)) {
		ret = efi_esrt_register();
		if (ret != EFI_SUCCESS)
			goto out;
	}

	if (IS_ENABLED(CONFIG_EFI_TCG2_PROTOCOL)) {
		ret = efi_tcg2_register();
		if (ret != EFI_SUCCESS)
			goto out;

		ret = efi_tcg2_do_initial_measurement();
		if (ret == EFI_SECURITY_VIOLATION)
			goto out;
	}

	/* Install EFI_RNG_PROTOCOL */
	if (IS_ENABLED(CONFIG_EFI_RNG_PROTOCOL)) {
		ret = efi_rng_register();
		if (ret != EFI_SUCCESS)
			goto out;
	}

	if (IS_ENABLED(CONFIG_EFI_RISCV_BOOT_PROTOCOL)) {
		ret = efi_riscv_register();
		if (ret != EFI_SUCCESS)
			goto out;
	}

	/* Secure boot */
	ret = efi_init_secure_boot();
	if (ret != EFI_SUCCESS)
		goto out;

	/* Indicate supported runtime services */
	ret = efi_init_runtime_supported();
	if (ret != EFI_SUCCESS)
		goto out;

	/* Initialize EFI driver uclass */
	ret = efi_driver_init();
	if (ret != EFI_SUCCESS)
		goto out;

	if (IS_ENABLED(CONFIG_EFI_HAVE_CAPSULE_SUPPORT)) {
		ret = efi_load_capsule_drivers();
		if (ret != EFI_SUCCESS)
			goto out;
	}

#if defined(CONFIG_LCD) || defined(CONFIG_DM_VIDEO)
	ret = efi_gop_register();
	if (ret != EFI_SUCCESS)
		goto out;
#endif
#ifdef CONFIG_EFI_NET
	ret = efi_net_register();
	if (ret != EFI_SUCCESS)
		goto out;
#endif
#ifdef CONFIG_GENERATE_ACPI_TABLE
	ret = efi_acpi_register();
	if (ret != EFI_SUCCESS)
		goto out;
#endif
#ifdef CONFIG_GENERATE_SMBIOS_TABLE
	ret = efi_smbios_register();
	if (ret != EFI_SUCCESS)
		goto out;
#endif
	ret = efi_watchdog_register();
	if (ret != EFI_SUCCESS)
		goto out;

	ret = efi_init_capsule();
	if (ret != EFI_SUCCESS)
		goto out;

	/* Initialize EFI runtime services */
	ret = efi_reset_system_init();
	if (ret != EFI_SUCCESS)
		goto out;

	/* Execute capsules after reboot */
	if (IS_ENABLED(CONFIG_EFI_CAPSULE_ON_DISK) &&
	    !IS_ENABLED(CONFIG_EFI_CAPSULE_ON_DISK_EARLY))
		ret = efi_launch_capsules();
out:
	efi_obj_list_initialized = ret;
	return ret;
}
