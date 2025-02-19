import subprocess
import sys
import os

def run_command(command, shell=True):
    """
    Runs a shell command and handles errors.
    
    Args:
        command (str): The command to execute.
        shell (bool): Whether to execute through the shell.
        
    Returns:
        subprocess.CompletedProcess: The result of the executed command.
    """
    try:
        print(f"Executing: {command}")
        result = subprocess.run(
            command,
            shell=shell,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        print(result.stdout)
        return result
    except subprocess.CalledProcessError as e:
        print(f"An error occurred while executing: {command}")
        print(e.stderr)
        sys.exit(1)

def run_powershell_script(script_path):
    """
    Executes a PowerShell script.
    
    Args:
        script_path (str): Path to the PowerShell script.
    """
    if not os.path.isfile(script_path):
        print(f"PowerShell script not found: {script_path}")
        sys.exit(1)
    command = f'powershell.exe -ExecutionPolicy Bypass -File "{script_path}"'
    run_command(command)

def execute_offset_calculator(exe_path, output_file):
    """
    Executes the offset_calculator.exe to generate addresses.
    
    Args:
        exe_path (str): Path to offset_calculator.exe.
        output_file (str): Path to the output file where addresses are written.
    """
    if not os.path.isfile(exe_path):
        print(f"Executable not found: {exe_path}")
        sys.exit(1)
    
    print("Running offset_calculator.exe...")
    run_command(f'"{exe_path}"')
    
    if not os.path.isfile(output_file):
        print(f"Expected output file not found: {output_file}")
        sys.exit(1)
    
    print(f"Addresses written to {output_file}")

def read_addresses(file_path):
    """
    Reads two addresses from the specified file.
    
    Args:
        file_path (str): Path to the file containing addresses.
        
    Returns:
        tuple: A tuple containing two addresses as strings.
    """
    addresses = []
    with open(file_path, 'r') as file:
        for line in file:
            # Assuming addresses are separated by spaces or commas
            parts = line.strip().replace(',', ' ').split()
            for part in parts:
                if part:
                    addresses.append(part)
                if len(addresses) == 2:
                    break
            if len(addresses) == 2:
                break
    
    if len(addresses) < 2:
        print("Error: Less than two addresses found in the file.")
        sys.exit(1)
    
    print(f"Address 1: {addresses[0]}")
    print(f"Address 2: {addresses[1]}")
    return addresses[0], addresses[1]

def get_process_id(process_name):
    """
    Retrieves the process ID of a given process name.
    
    Args:
        process_name (str): The name of the process to find.
        
    Returns:
        str: The process ID as a string.
    """
    try:
        result = subprocess.run(
            ['tasklist', '/FI', f'IMAGENAME eq {process_name}', '/NH'],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=True
        )
        output = result.stdout.strip()
        if not output or "No tasks" in output:
            print(f"Process {process_name} not found.")
            sys.exit(1)
        # Parse the PID from the output
        # The output format is: Image Name | PID | Session Name | Session# | Mem Usage
        # Split by whitespace, first field is image name, second is PID
        parts = output.split()
        pid = parts[1]
        print(f"Found {process_name} with PID: {pid}")
        return pid
    except subprocess.CalledProcessError as e:
        print(f"Error finding process {process_name}: {e.stderr}")
        sys.exit(1)

def execute_hypervisor_app(app_path, address, type_):
    """
    Executes MyHypervisorApp.exe with the given address and type.
    
    Args:
        app_path (str): Path to MyHypervisorApp.exe.
        address (str): The address to pass as an argument.
        type_ (str): The type to pass as an argument.
    """
    if not os.path.isfile(app_path):
        print(f"Executable not found: {app_path}")
        sys.exit(1)
    
    pid = get_process_id("Overwatch.exe")
    command = f'"{app_path}" {pid} {address} {type_}'
    run_command(command)

def main():
    # Define paths (adjust these paths as necessary)
    script_dir = os.path.dirname(os.path.abspath(__file__))
    offset_calculator_exe = os.path.join(script_dir, "instruction_offset_calculator.exe")
    instruction_addresses_file = os.path.join(script_dir, "instruction_addresses.txt")
    stop_hypervisor_script = os.path.join(script_dir, "stop_hypervisor.ps1")
    run_hypervisor_script = os.path.join(script_dir, "run_hypervisor.ps1")
    hypervisor_app_exe = os.path.join(script_dir, "MyHypervisorApp.exe")
    
    # Step 1: Execute instruction_offset_calculator.exe to add the target app's base address to
    # the target instrunction's offset and save the calculated instruction addresses to the file.
    execute_offset_calculator(offset_calculator_exe, instruction_addresses_file)
    
    # Step 2: Read the found instruction addresses
    address1, address2 = read_addresses(instruction_addresses_file)
    
    # Step 3: Execute stop_hypervisor.ps1
    run_powershell_script(stop_hypervisor_script)
    # Step 4: Execute run_hypervisor.ps1
    run_powershell_script(run_hypervisor_script)
    
    # Step 5: Execute MyHypervisorApp.exe with address1 and type "2"
    # In ept.c : EptHandleHookedPage(), type "2" is for finding the address where player's position info is stored
    execute_hypervisor_app(hypervisor_app_exe, address1, "2")
    
    # Step 6: Execute stop_hypervisor.ps1
    run_powershell_script(stop_hypervisor_script)
    # Step 7: Execute run_hypervisor.ps1
    run_powershell_script(run_hypervisor_script)
    
    # Step 8: Execute MyHypervisorApp.exe with address2 and type "3"
    # In ept.c : EptHandleHookedPage(), type "3" is for finding the address where enemies' position info is stored
    execute_hypervisor_app(hypervisor_app_exe, address2, "3")
    
    print("All steps completed successfully.")

if __name__ == "__main__":
    main()
