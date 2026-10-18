import json, sys, os, subprocess, time, shutil

CONFIG_FILE = "config2.json"
REPORT_FILE = "Test_Suite_Report.txt"

def handle_json(tasks=None, policy="ePolicySkip", ps_config=None):
    """Load (if tasks=None) or save the JSON compactly."""
    if tasks is not None:
        with open(CONFIG_FILE, "w") as f:
            json.dump({"policy": policy, "tasks": tasks, "ps_config": ps_config}, f, indent=4)
        return tasks, policy, ps_config
    try:
        with open(CONFIG_FILE, "r") as f:
            data = json.load(f)
            return data.get("tasks", []), data.get("policy", "ePolicySkip"), data.get("ps_config", None)
    except (FileNotFoundError, json.JSONDecodeError):
        return [], "ePolicySkip", None

def run_self_assessment_test():
    print(f"\n=== MDE Test Suite: Analysis Tests 1-21 ===")
    
    # Backup the current main.c
    try: shutil.copyfile("main.c", "main_backup_mde.c")
    except: pass

    # Create/Clear the Unified Report file
    with open(REPORT_FILE, "w", encoding="utf-8") as f:
        f.write("========== COMPLETE TEST SUITE REPORT ==========\n\n")

    passed_count = 0
    for i in range(1, 22):
        print(f"[*] Running QEMU simulation for [Test {i}]...")
        try: shutil.copyfile(f"Tests/test{i}.c", "main.c")
        except FileNotFoundError:
            print(f"[-] TEST {i}: File missing.\n")
            continue
            
        if os.system("make clean > /dev/null 2>&1 && make all > build_log.txt 2>&1") != 0:
            print(f"    [-] FAIL. Build Log:")
            os.system("cat build_log.txt")
            continue
            
        try:
            # QEMU runs isolated so the emulator won't be orphaned in the background
            proc = subprocess.Popen(
                ["qemu-system-arm", "-machine", "mps2-an385", "-cpu", "cortex-m3", "-kernel", "./Output/demo.elf", "-monitor", "none", "-nographic", "-serial", "stdio"],
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
            )
            
            import select
            start_t = time.time()
            out_lines = []
            while time.time() - start_t < 15:
                r, _, _ = select.select([proc.stdout], [], [], 0.5)
                if r:
                    line = proc.stdout.readline()
                    if line:
                        out_lines.append(line)
                        if "[VERDICT]" in line:
                            # Let it read any immediate trailing lines like verdict details
                            time.sleep(0.2)
                            break
                    else:
                        break
                if proc.poll() is not None:
                    break
            
            proc.terminate()
            proc.wait()
            rem, _ = proc.communicate()
            if rem:
                out_lines.append(rem)
                
            out = "".join(out_lines)
            if out.strip():
                if "[VERDICT] PASS" in out:
                    print(f"    -> [+] PASS (C-Level Verdict)")
                    passed_count += 1
                elif "[VERDICT] FAIL" in out:
                    print(f"    -> [-] FAIL (C-Level Verdict)")
                else:
                    print(f"    -> [?] UNKNOWN (No verdict printed, {len(out)} chars)")
                    
                # Append the trace to the Unified Report file
                with open(REPORT_FILE, "a", encoding="utf-8") as f:
                    f.write(f"\n--- START TRACE [TEST {i}] ---\n{out}\n--- END TRACE [TEST {i}] ---\n\n")
            else:
                print(f"    -> [-] FAIL (No UART Trace)")
                
        except Exception as e:
            print(f"    -> [-] CRASH ({e})")
            
    # Restore main.c
    try:
        shutil.copyfile("main_backup_mde.c", "main.c")
        os.remove("main_backup_mde.c")
    except: pass
    
    print(f"\n=> {passed_count}/21 Tests passed.")
    print(f"=> All logs are merged and can be inspected in '{REPORT_FILE}'.\n")
    sys.exit(0)

def interactive_menu():
    print("\n===============================")
    print("   MDE Configurator & Runner")
    print("===============================")
    print(f"1. Load from '{CONFIG_FILE}'\n2. Create Tasks manually\n3. Run Test Suite (1-21)")
    try: choice = input("\n> Choice [1/2/3]: ").strip()
    except: sys.exit(0)
    
    if choice == '1':
        t, p, ps = handle_json()
        if not t and ps is None:
            print(f"[-] {CONFIG_FILE} is empty or missing.")
            sys.exit(1)
        print(f"[+] Architecture loaded (Active policy: {p})")
        return t, p, ps
        
    elif choice == '2':
        tasks = []
        while True:
            try: name = input("Task Name (e.g. Task1, 'q' to quit): ").strip()
            except: sys.exit(0)
            if name.lower() == 'q': break
            if not name: continue
            try:
                period = int(input("  Period (ms): "))
                compute = int(input("  Compute Time (ms): "))
                deadline_str = input("  Deadline (ms) [Enter for default=Period]: ").strip()
                deadline = int(deadline_str) if deadline_str else period
                offset_str = input("  Phase/Offset (ms) [Enter for default=0]: ").strip()
                offset = int(offset_str) if offset_str else 0
                tasks.append({
                    "name": name, 
                    "period": period, 
                    "compute": compute,
                    "deadline": deadline,
                    "offset": offset
                })
            except ValueError: print("  [!] Please enter numeric values.")
            
        if not tasks: sys.exit(0)
        
        print("\nChoose the Global Overrun Policy for the Operating System:")
        print("1) SKIP (Skip new task release, let the old one finish in overrun)")
        print("2) KILL (Ruthlessly kill the old task, start the new one)")
        print("3) CATCH_UP (Run them sequentially, log the miss but keep the pace)")
        pc = input("> [1/2/3]: ").strip()
        pmap = {'1': 'ePolicySkip', '2': 'ePolicyKill', '3': 'ePolicyCatchUp'}
        policy = pmap.get(pc, 'ePolicySkip')
        
        ps_config = None
        has_ps = input("\nEnable Polling Server for Aperiodic Tasks? [y/N]: ").strip().lower()
        if has_ps == 'y':
            try:
                period = int(input("  Polling Server Period (ms): "))
                capacity = int(input("  Polling Server Capacity (ms): "))
                ps_config = {"period": period, "capacity": capacity, "tasks": [], "random_injection": False}
                
                print("\n  --- Aperiodic Tasks ---")
                while True:
                    ap_name = input("  Aperiodic Task Name (e.g. ApTask1, 'q' to finish): ").strip()
                    if ap_name.lower() == 'q': break
                    if not ap_name: continue
                    try:
                        ap_compute = int(input("    Compute Time (ms): "))
                        ap_deadline_str = input("    Soft Deadline (ms) [Enter for default=100]: ").strip()
                        ap_deadline = int(ap_deadline_str) if ap_deadline_str else 100
                        ap_priority_str = input("    Priority (0-255) [Enter for default=10]: ").strip()
                        ap_priority = int(ap_priority_str) if ap_priority_str else 10
                        ps_config["tasks"].append({
                            "name": ap_name,
                            "compute": ap_compute,
                            "deadline": ap_deadline,
                            "priority": ap_priority
                        })
                    except ValueError: print("    [!] Please enter numeric values.")
                    
                if ps_config["tasks"]:
                    rand_inj = input("\n  Inject tasks randomly at runtime? [Y/n]: ").strip().lower()
                    if rand_inj != 'n':
                        ps_config["random_injection"] = True
            except ValueError:
                print("  [!] Invalid numeric input. Defaults applied: T=500, C=200")
                ps_config = {"period": 500, "capacity": 200, "tasks": [], "random_injection": False}
                
        # Check utilization before saving to prevent corrupting CONFIG_FILE
        utilization = sum(t["compute"] / t["period"] for t in tasks)
        if ps_config:
            utilization += ps_config["capacity"] / ps_config["period"]
        if utilization > 1.0:
            print(f"\n[*] Schedulability Analysis: CPU {utilization*100:.1f}%")
            print("[-] WARNING: CPU Utilization exceeds 100%. The system is statically overloaded.")
            if input("  -> Do you want to save this configuration and proceed anyway? [y/N]: ").strip().lower() != 'y':
                print("Aborted by user.")
                sys.exit(1)
            
        return handle_json(tasks, policy, ps_config)
        
    elif choice == '3':
        run_self_assessment_test()
        
    else:
        sys.exit(1)

def create_main_c(tasks, policy, ps_config=None):
    print("\n[*] Generating 'main.c' Architecture...")
    
    has_ps = ps_config is not None
    num_tasks = len(tasks) + (1 if has_ps else 0)
    
    headers = """#include "FreeRTOS.h"
#include "task.h"
#include "uart.h"
#include "ptl_config.h"
#include "ptl_scheduler.h"
"""
    if has_ps:
        headers += """#include "PollingServer.h"
#include "aperiodic_tasks.h"
"""
        if ps_config.get("random_injection"):
            headers += "#include <stdlib.h>\n"

    bodies = "".join(
        f"static void prv{t['name']}Body(void *pvArgs) {{\n"
        f"  (void)pvArgs;\n"
        f"  vUartPrintf(\"[RTOS] >> {t['name']} wake up.\\n\");\n"
        f"  TickType_t xStart = xTaskGetTickCount();\n"
        f"  while((xTaskGetTickCount() - xStart) < pdMS_TO_TICKS({t['compute']}UL)) {{}}\n"
        "}\n\n" for t in tasks
    )

    ps_tasks = ps_config.get("tasks", []) if has_ps else []
    if has_ps and ps_tasks:
        bodies += "".join(
            f"static void prv{t['name']}Body(void *pvArgs) {{\n"
            f"  (void)pvArgs;\n"
            f"  vUartPrintf(\"[RTOS] >> Aperiodic {t['name']} running.\\n\");\n"
            f"  TickType_t xStart = xTaskGetTickCount();\n"
            f"  while((xTaskGetTickCount() - xStart) < pdMS_TO_TICKS({t['compute']}UL)) {{}}\n"
            "}\n\n" for t in ps_tasks
        )

        if ps_config.get("random_injection"):
            bodies += "static void vRandomSpawnerTask(void *pvArgs) {\n"
            bodies += "  (void)pvArgs;\n"
            bodies += "  for(;;) {\n"
            bodies += "    TickType_t delay = pdMS_TO_TICKS((rand() % 250) + 50);\n"
            bodies += "    vTaskDelay(delay);\n"
            bodies += "    uint8_t choice = rand() % " + str(len(ps_tasks)) + ";\n"
            bodies += "    switch(choice) {\n"
            for idx, t in enumerate(ps_tasks):
                bodies += f"      case {idx}:\n"
                bodies += f'        xAperiodicSubmitJob(prv{t["name"]}Body, NULL, "{t["name"]}", pdMS_TO_TICKS({t["deadline"]}UL), {t["priority"]}U);\n'
                bodies += f'        break;\n'
            bodies += "    }\n"
            bodies += "  }\n"
            bodies += "}\n\n"

    # Reporter Task to dump trace and stats on receiving 'q' or Ctrl+C character (ETX) over UART
    bodies += """static void vReporterTask(void *pvArgs) {
  (void)pvArgs;
  for(;;) {
    if ((UART0_STATE & 2) != 0) {
      uint8_t c = (uint8_t)UART0_DATA;
      if (c == 'q' || c == 3) {
        vUartPrintf("\\n--- CUSTOM SIMULATION TRACE DUMP ---\\n");
        vTraceDump();
        vUartPrintf("--- END ---\\n");
        
        PTLTaskStats_t stats[10];
        UBaseType_t num_tasks = uxPtlGetStats(stats, 10);
        vTraceStatsDump(stats, num_tasks);
        
        vUartPrintf("[SIMULATION DONE]\\n");
        for(;;);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

"""

    # Rate Monotonic Priority Assignment
    all_tasks_for_rm = [{"name": t["name"], "period": t["period"]} for t in tasks]
    if has_ps:
        all_tasks_for_rm.append({"name": "PollSrv", "period": ps_config["period"]})
        
    all_tasks_for_rm.sort(key=lambda x: x["period"])
    current_prio = 10
    last_period = None
    task_priorities = {}
    for t in all_tasks_for_rm:
        if last_period is not None and t["period"] > last_period:
            if current_prio > 2:
                current_prio -= 1
        task_priorities[t["name"]] = current_prio
        last_period = t["period"]

    ps_setup = ""
    if has_ps:
        ps_setup = f"""static PSConfig_t xPSConfig = {{
  .uxPriority = tskIDLE_PRIORITY+{task_priorities["PollSrv"]},
  .ulStackSize = 512U,
  .ulPeriodMs = {ps_config['period']}U,
  .ulCapacityMs = {ps_config['capacity']}U,
  .xPolicy = PS_POLICY_OVERRUN
}};
"""

    array_entries = "".join(
        f'  {{"{t["name"]}", prv{t["name"]}Body, NULL, 512U, tskIDLE_PRIORITY+{task_priorities[t["name"]]}, {t["period"]}U, {t.get("deadline", t["period"])}U, {t.get("offset", 0)}U, eTracePeriodic, ePolicySkip}},\n' for t in tasks
    )
    if has_ps:
        array_entries += f'  {{"PollSrv", vPollingServer_Task, NULL, 512U, tskIDLE_PRIORITY+{task_priorities["PollSrv"]}, {ps_config["period"]}U, 0U, 0U, eTracePollingServer, ePolicySkip}},\n'

    ps_init = ""
    if has_ps:
        ps_init = """
  vAperiodicInit();
  if(xPollingServer_Init(&xPSConfig) != pdPASS) {
      vUartPrintf("ERROR: Polling Server Init failed\\n");
      for(;;);
  }"""
        if ps_tasks:
            if ps_config.get("random_injection"):
                ps_init += '\n  xTaskCreate(vRandomSpawnerTask, "Spawner", 256U, NULL, tskIDLE_PRIORITY+1, NULL);'
            else:
                submit_lines = "".join(
                    f'\n  xAperiodicSubmitJob(prv{t["name"]}Body, NULL, "{t["name"]}", pdMS_TO_TICKS({t["deadline"]}UL), {t["priority"]}U);' for t in ps_tasks
                )
                ps_init += submit_lines

    c_code = f"""{headers}
{ps_setup}
{bodies}
static PTLTaskConfig_t xTasks[{num_tasks}] = {{
{array_entries}}};

static PTLSchedulerConfig_t xAppConfig = {{
  .xPolicy = {policy},
  .xTraceEnabled = pdTRUE,
  .ucNumTasks = {num_tasks}
}};

int main(void) {{
  vUartInit();
  UART0_CTRL = 3; /* Enable RX so we can receive the 'q' stop command */
  vUartPrintf("\\n=== MDE RTOS Auto-Generated ===\\n\\n");{ps_init}
  for(uint8_t ucIdx=0; ucIdx<{num_tasks}; ucIdx++) xAppConfig.xTasks[ucIdx] = xTasks[ucIdx];
  if (xPtlInit(&xAppConfig) != pdPASS) for(;;);
  xTaskCreate(vReporterTask, "Reporter", 512U, NULL, tskIDLE_PRIORITY + 14U, NULL);
  vPtlStart();
  vTaskStartScheduler();
  for(;;);
}}
"""
    with open("main.c", "w") as f:
        f.write(c_code)

if __name__ == "__main__":
    tasks, policy, ps_config = interactive_menu()
    
    utilization = sum(t["compute"] / t["period"] for t in tasks)
    if ps_config:
        utilization += ps_config["capacity"] / ps_config["period"]
        
    print(f"\n[*] Schedulability Analysis: CPU {utilization*100:.1f}%")
    if utilization > 1.0:
        print("[-] WARNING: CPU Utilization exceeds 100%. The system is statically overloaded.")
        if input("  -> Do you want to proceed with code generation and simulation anyway? [y/N]: ").strip().lower() != 'y':
            print("Generation aborted by user.")
            sys.exit(1)
        
    # Check if all tasks have Implicit Deadlines (D == T). RM Bounds require this.
    implicit_deadlines = all(t.get("deadline", t["period"]) == t["period"] for t in tasks)
    
    if implicit_deadlines:
        # Rate Monotonic Hyperbolic Bound Analysis
        hyperbolic_product = 1.0
        for t in tasks:
            u_i = t["compute"] / t["period"]
            hyperbolic_product *= (u_i + 1.0)
            
        if ps_config:
            u_s = ps_config["capacity"] / ps_config["period"]
            hyperbolic_bound = 2.0 / (u_s + 1.0)
        else:
            hyperbolic_bound = 2.0
            
        print(f"[*] RM Hyperbolic Bound: Product(Ui+1) = {hyperbolic_product:.4f} <= Bound({hyperbolic_bound:.4f})")
        if hyperbolic_product <= hyperbolic_bound:
            print("[+] Schedulability GUARANTEED by Hyperbolic Bound.")
        else:
            print("[-] WARNING: Hyperbolic Bound NOT met. The system might miss deadlines.")
            if input("  -> Do you want to proceed with code generation anyway? [y/N]: ").strip().lower() != 'y':
                print("Generation aborted by user.")
                sys.exit(1)
    else:
        print("[-] WARNING: Constrained deadlines (D != T) detected in the task set.")
        print("    The Hyperbolic Bound is valid ONLY for implicit deadlines.")
        print("    Schedulability is NOT guaranteed by simple utilization bounds. Full Response Time Analysis (RTA) is required.")
        if input("  -> Do you want to proceed with code generation anyway? [y/N]: ").strip().lower() != 'y':
            print("Generation aborted by user.")
            sys.exit(1)

    create_main_c(tasks, policy, ps_config)
    
    print("\n[*] Building OS Kernel...")
    if os.system("make clean > /dev/null 2>&1 && make all > /dev/null 2>&1") != 0:
        print("[-] Build failed completely.")
        sys.exit(1)
        
    print("[+] Compilation OK. Booting OS. [Press Ctrl+C to stop simulation]")
    time.sleep(1)
    
    try:
        proc = subprocess.Popen(
            ["qemu-system-arm", "-machine", "mps2-an385", "-cpu", "cortex-m3", "-kernel", "./Output/demo.elf", "-monitor", "none", "-nographic", "-serial", "stdio"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
            start_new_session=True
        )
        
        out_lines = []
        try:
            while True:
                line = proc.stdout.readline()
                if not line:
                    break
                print(line, end="")
                out_lines.append(line)
        except KeyboardInterrupt:
            print("\n[*] Stopping simulation, querying scheduler stats and generating report...")
            try:
                proc.stdin.write('q\n')
                proc.stdin.flush()
                # Read remaining lines until QEMU finishes printing the reports
                start_stop_t = time.time()
                while time.time() - start_stop_t < 2.0:
                    line = proc.stdout.readline()
                    if not line:
                        break
                    print(line, end="")
                    out_lines.append(line)
                    if "[SIMULATION DONE]" in line:
                        break
            except Exception as e:
                print(f"[-] Failed to finalize report: {e}")
        finally:
            proc.terminate()
            proc.wait()
            
        # Write trace to Simulation_Report.txt
        with open("Simulation_Report.txt", "w", encoding="utf-8") as f:
            f.write("========== CUSTOM SIMULATION REPORT ==========\n\n")
            f.write("".join(out_lines))
        print("[+] Report saved to 'Simulation_Report.txt'\n")
    except Exception as e:
        print(f"[-] Error during simulation: {e}")
