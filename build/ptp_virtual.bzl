load("//build/kernel/kleaf:kernel.bzl", "ddk_module")
load("//build/bazel_common_rules/dist:dist.bzl", "copy_to_dist_dir")

def ptp_get_srcs():
    srcs = [
        "ptp_virtual_module.c",
    ]

    return srcs

def define_target_variant_module(target, variant):
    tv = "{}_{}".format(target, variant)
    rule_name = "{}_ptp_virtual".format(tv)
    kernel_build = "//soc-repo:{}_base_kernel".format(tv)

    ddk_module(
        name = rule_name,
        out = "ptp_virtual.ko",
        srcs = ptp_get_srcs(),
        deps = [
            "//soc-repo:all_headers"],
        kernel_build = kernel_build,
        visibility = ["//visibility:public"]
    )

    copy_to_dist_dir(
        name = "{}_dist".format(rule_name),
        data = [rule_name],
        dist_dir = "out/target/product/{}/dlkm/lib/modules/".format(target),
        flat = True,
        wipe_dist_dir = False,
        allow_duplicate_filenames = False,
        mode_overrides = {"**/*": "644"},
        log = "info",
    )

def define_target_module(target):
    define_target_variant_module(target, "perf")
    define_target_variant_module(target, "consolidate")
    define_target_variant_module(target, "gki")
