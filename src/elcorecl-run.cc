// Copyright 2019-2020 RnD Center "ELVEES", JSC
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <vector>

#include <stdio.h>

#include <err.h>
#include <error.h>
#include <getopt.h>
#include <unistd.h>

#include <elcorecl/elcorecl.h>

bool USE_ALL_CORES = 0;

void help() {
    printf("Run ElcoreCL kernel on DSP\n");
    printf(" -e <file> \t ELF ElcoreCL kernel file to run (mandatory)\n");
    printf(" -s <count> \t size of shared memory in bytes\n");
    printf(" --core=<cores> \t comma separated list of cores or ranges, e.g. 0,4-6,9 "
           "or `all` to select all available cores, default: 0\n");
    printf(
        " --init-sync-file <file-name> \t create file <file-name> after initialization is "
        "completed\n");
    printf(" --wait-for-file <file-name> \t wait for <file-name> is created before start jobs\n");
    printf(" -- <list of arguments> \t set arguments to kernel. This arguments will be passed to"
           "main() in kernel\n");
}

void ECL_CALLBACK MemoryDestructor(ecl_mem, void *user_data) { free(user_data); }

void *AllocateAlign(size_t &size) {
    void *p = nullptr;
    const int page_size = getpagesize();
    size = ((size + page_size - 1) / page_size) * page_size;
    if (posix_memalign(&p, page_size, size) != 0 || p == nullptr) {
        fprintf(stderr, "Memory allocation failed\n");
        return nullptr;
    }
    return p;
}

ecl_int CreateBuffer(ecl_context context, size_t size, ecl_mem &mem, void *p) {
    ecl_int result;
    mem = eclCreateBuffer(context, ECL_MEM_USE_HOST_PTR, size, p, &result);
    if (mem == nullptr || result != ECL_SUCCESS) {
        fprintf(stderr, "Function eclCreateBuffer failed. Error code: %d\n", result);
        return result;
    }
    result = eclSetMemObjectDestructorCallback(mem, MemoryDestructor, p);
    if (result != ECL_SUCCESS) {
        fprintf(stderr, "Function eclSetMemObjectDestructorCallback failed. Error code: %d\n",
                result);
        return result;
    }
    return ECL_SUCCESS;
}

void wait_for_sync(const char *file_name) {
    FILE *ready;

    fprintf(stdout, "%s: waiting for sync\n", __func__);
    /* We might lose about 2ms worth of data */
    do {
        /* Sleep for 2ms */
        usleep(2000);
        ready = fopen(file_name, "r");
    } while (!ready);
    fclose(ready);
}

std::set<ecl_uint> parse_cores(const std::string str_cores) {
    std::set<ecl_uint> cores;
    std::string s;
    std::istringstream stream(str_cores);

    if (str_cores.find("all") != std::string::npos) {
        USE_ALL_CORES = 1;
        return cores;
    }

    while (getline(stream, s, ',')) {
        int dash_pos = s.find('-');
        if (dash_pos == std::string::npos) {
            cores.insert(std::stoi(s));
        } else {
            int start_core = std::stoi(s.substr(0, dash_pos));
            int end_core = std::stoi(s.substr(dash_pos + 1));

            for (int core = start_core; core <= end_core; ++core)
                cores.insert(core);
        }
    }

    return cores;
}

int main(int argc, char **argv) {
    int opt, ret;
    ecl_uint ncores;
    char *func_name, *elf;
    size_t shmem_size = 0;
    elf = NULL;
    func_name = "_elcore_main_wrapper";
    std::set<ecl_uint> cores;
    static struct option long_options[] = {{"init-sync-file", required_argument, 0, 0},
                                           {"wait-for-file", required_argument, 0, 0},
                                           {"core", optional_argument, 0, 0},
                                           {0, 0, 0, 0}};
    int option_index = 0;
    char *init_sync_file = NULL, *wait_for_file = NULL;


    while ((opt = getopt_long(argc, argv, "he:f:s:", long_options, &option_index)) != -1) {
        switch (opt) {
            case 0:
                switch (option_index) {
                    case 0:
                        init_sync_file = optarg;
                        break;
                    case 1:
                        wait_for_file = optarg;
                        break;
                    case 2:
                        cores = parse_cores(optarg);
                        if ((USE_ALL_CORES == 0) && (cores.size() == 0))
                            error(EXIT_FAILURE, errno, "Failed to parse cores");
                        ncores = cores.size();
                        break;
                }
                break;
            case 'h':
                help();
                return EXIT_SUCCESS;
            case 'e':
                elf = optarg;
                break;
            case 's':
                shmem_size = atoi(optarg);
                func_name = "_elcorecl_run_wrapper";
                break;
            default:
                error(EXIT_FAILURE, errno, "Try %s -h for help.\n", argv[0]);
        }
    }
    if (elf == NULL) errx(1, "Elf file is not specified");

    std::vector<std::string> kernel_arguments;
    kernel_arguments.push_back(elf);  // the program name is the first argument
    while (optind < argc)
        kernel_arguments.push_back(argv[optind++]);
    size_t kernel_arguments_size = 0;
    for (int i = 0; i < kernel_arguments.size(); ++i)
        kernel_arguments_size += kernel_arguments[i].size();
    kernel_arguments_size += kernel_arguments.size() + 1;  // '\0' separators

    size_t kernel_arguments_size_aligned = kernel_arguments_size;
    char *kernel_arguments_aligned = (char *)AllocateAlign(kernel_arguments_size_aligned);
    size_t offset = 0;
    for (int i = 0; i < kernel_arguments.size(); ++i) {
        memcpy((void *)&kernel_arguments_aligned[offset], kernel_arguments[i].c_str(),
               kernel_arguments[i].size());
        offset += kernel_arguments[i].size();
        kernel_arguments_aligned[offset++] = '\0';
    }
    kernel_arguments_aligned[offset] = '\0';  // the final empty string

    ecl_platform_id platform_id;
    ret = eclGetPlatformIDs(1, &platform_id, nullptr);
    if (ret != ECL_SUCCESS) errx(1, "Failed to get platform id. Error code: %d", ret);
    ecl_uint ndevs = 0;
    ret = eclGetDeviceIDs(platform_id, ECL_DEVICE_TYPE_CUSTOM, 0, nullptr, &ndevs);
    if (ret != ECL_SUCCESS) errx(1, "Failed to get device id. Error code: %d", ret);
    if (USE_ALL_CORES) {
        ncores = ndevs;
        for (int i = 0; i < ncores; ++i)
            cores.insert(i);
    }
    if (cores.size() == 0) {
        ncores = 1;
        cores.insert(0);
    }

    if (*cores.rbegin() >= ndevs) errx(1, "Specified wrong core: %d\n", *cores.rbegin());

    std::vector<ecl_device_id> all_devices(ndevs);
    ret = eclGetDeviceIDs(platform_id, ECL_DEVICE_TYPE_CUSTOM, ndevs, &all_devices[0], nullptr);
    if (ret != ECL_SUCCESS) errx(1, "Failed to get device id. Error code: %d", ret);

    std::vector<ecl_device_id> selected_devices;
    for (auto it = cores.begin(); it != cores.end(); ++it)
        selected_devices.push_back(all_devices[*it]);
    all_devices.clear();
    printf("ncores=%d ndevs=%d\n", ncores, ndevs);
    if (ndevs < ncores)
        errx(1, "The number of available devices=%d is less than requested=%d", ndevs, ncores);

    ecl_int result;
    ecl_context context =
        eclCreateContext(nullptr, ncores, &selected_devices[0], nullptr, nullptr, &result);
    if (context == nullptr || result != ECL_SUCCESS)
        errx(1, "Failed to create context. Error code: %d", result);

    std::vector<char> elf_buffer;
    size_t elf_size[ncores];
    {
        std::ifstream file(elf, std::ios::binary | std::ios::ate);
        if (!file) errx(1, "Failed to open %s. Error code: %d", elf, errno);
        elf_size[0] = file.tellg();
        file.seekg(0, std::ios::beg);
        elf_buffer.resize(elf_size[0]);
        file.read(elf_buffer.data(), elf_size[0]);
    }

    const unsigned char *elfs[ncores];
    for (int i = 0; i < ncores; ++i) {
        elf_size[i] = elf_size[0];
        elfs[i] = reinterpret_cast<unsigned char *>(elf_buffer.data());
    }
    ecl_program program = eclCreateProgramWithBinary(context, ncores, &selected_devices[0],
                                                     &elf_size[0], &elfs[0], nullptr, &result);
    if (program == nullptr || result != ECL_SUCCESS) {
        errx(1, "Failed to create program. Error code: %d", result);
    }

    ecl_kernel kernel = eclCreateKernel(program, func_name, &result);
    if (kernel == nullptr || result != ECL_SUCCESS)
        errx(1, "Failed to create kernel. Error code: %d", result);

    ecl_mem shmem_res;
    char *shmem_buf;
    if (shmem_size) {
        shmem_buf = reinterpret_cast<char *>(AllocateAlign(shmem_size));
        memset(shmem_buf, 0, shmem_size);
        CreateBuffer(context, shmem_size, shmem_res, shmem_buf);
        if ((shmem_buf == nullptr) || (shmem_res == nullptr))
            errx(1, "Failed to create shared buffer");
    }

    // Create buffer with argc/argv
    ecl_mem args_res;
    CreateBuffer(context, kernel_arguments_size_aligned, args_res, kernel_arguments_aligned);
    if (args_res == nullptr)
        errx(1, "Failed to create buffer for argc/argv");

    if (init_sync_file) {
        std::stringstream ss;
        ss << "touch " << init_sync_file;
        std::system(ss.str().c_str());
    }

    if (wait_for_file) {
        wait_for_sync(wait_for_file);
    }

    ecl_event kernel_event[ncores];
    ecl_command_queue queue[ncores];
    ecl_mem retvals_res[ncores];
    ecl_uint *retvals[ncores];
    size_t retval_size = sizeof(ecl_uint);
    for (int i = 0; i < ncores; ++i) {
        retvals[i] = reinterpret_cast<ecl_uint *>(AllocateAlign(retval_size));
        *retvals[i] = 0;
        CreateBuffer(context, retval_size, retvals_res[i], retvals[i]);
        if ((retvals[i] == nullptr) || (retvals_res[i] == nullptr))
            errx(1, "Failed to create retval buffer");
    }
    printf("run");
    auto core_num = cores.begin();
    for (int i = 0; i < ncores; ++i, ++core_num) {
        printf(" %d", *core_num);
        queue[i] = eclCreateCommandQueueWithProperties(context, selected_devices[i], nullptr,
                                                       &result);
        if (queue[i] == nullptr || result != ECL_SUCCESS)
            errx(1, "Failed to create queue for device %d. Error code: %d", *core_num, result);

        ecl_uint iarg = 0;
        // Pass buffer with user arguments
        ret = eclSetKernelArgELcoreMem(kernel, iarg++, args_res);
        if (ret != ECL_SUCCESS)
            errx(1, "Failed to set %d arg for device %d. Error code: %d", iarg - 1,
                 *core_num, ret);
        // Pass retval buffer
        ret = eclSetKernelArgELcoreMem(kernel, iarg++, retvals_res[i]);
        if (ret != ECL_SUCCESS)
            errx(1, "Failed to set %d arg for device %d. Error code: %d", iarg - 1,
                 *core_num, ret);

        if (shmem_size) {
            ret = eclSetKernelArgELcoreMem(kernel, iarg++, shmem_res);
            if (ret != ECL_SUCCESS)
                errx(1, "Failed to set %d arg for device %d. Error code: %d", iarg - 1, *core_num,
                     ret);
            ret = eclSetKernelArg(kernel, iarg++, sizeof(int32_t), &shmem_size);
            if (ret != ECL_SUCCESS)
                errx(1, "Failed to set %d arg for device %d. Error code: %d", iarg - 1, *core_num,
                     ret);
        }

        const size_t global_work_size[1] = {1};
        ret = eclEnqueueNDRangeKernel(queue[i], kernel, 1, nullptr, global_work_size, nullptr, 0,
                                      nullptr, kernel_event + i);
        if (ret != ECL_SUCCESS)
            errx(1, "Failed to enqueued kernel for device %d. Error code: %d", *core_num, ret);
    }
    printf(" and wait all %d cores\n", ncores);
    ret = eclWaitForEvents(ncores, kernel_event);
    if (ret != ECL_SUCCESS) errx(1, "Failed to wait for event. Error code: %d", ret);

    core_num = cores.begin();
    for (int i = 0; i < ncores; ++i, ++core_num) {
        eclEnqueueMapBuffer(queue[i], retvals_res[i], ECL_TRUE, ECL_MAP_READ, 0,
                            retval_size, 0, NULL, NULL, &result);
        ret = eclReleaseCommandQueue(queue[i]);
        if (ret != ECL_SUCCESS) errx(1, "Failed to release queue. Error code: %d", ret);
    }

    if (shmem_size) {
        ret = eclReleaseMemObject(shmem_res);
        if (ret != ECL_SUCCESS) errx(1, "Failed to release resource. Error code: %d", ret);
    }

    ret = eclReleaseMemObject(args_res);
    if (ret != ECL_SUCCESS) errx(1, "Failed to release resource. Error code: %d", ret);

    ret = eclReleaseKernel(kernel);
    if (ret != ECL_SUCCESS) errx(1, "Failed to release kernel. Error code: %d", ret);
    ret = eclReleaseProgram(program);
    if (ret != ECL_SUCCESS) errx(1, "Failed to release program. Error code: %d", ret);
    ret = eclReleaseContext(context);
    if (ret != ECL_SUCCESS) errx(1, "Failed to release context. Error code: %d", ret);

    for (int i = 0; i < ncores; ++i) {
        if (*retvals[i] != 0) return *retvals[i];
        ret = eclReleaseMemObject(retvals_res[i]);
        if (ret != ECL_SUCCESS) errx(1, "Failed to release resource. Error code: %d", ret);
    }
    return 0;
}