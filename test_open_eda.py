import os
import sys

# Add the build directory to the Python path so it can find open_eda.pyd or open_eda.so
build_dir = os.path.join(os.path.dirname(__file__), 'build')
if build_dir not in sys.path:
    sys.path.append(build_dir)
# For Windows Debug builds, it might be in build/Debug
debug_build_dir = os.path.join(build_dir, 'Debug')
if debug_build_dir not in sys.path:
    sys.path.append(debug_build_dir)
# For Windows Release builds
release_build_dir = os.path.join(build_dir, 'Release')
if release_build_dir not in sys.path:
    sys.path.append(release_build_dir)

import open_eda

def main():
    print("=== OpenEDA Python Shell Interface ===")
    
    # 1. Initialize the database
    print("[1/4] Initializing Design Database...")
    chip = open_eda.Design()
    
    # We use a benchmark verilog file that exists or standard alu structural
    # Assuming the user has "alu_32bit_structural.v", let's load it
    verilog_file = "alu_32bit_structural.v"
    
    if os.path.exists(verilog_file):
        chip.load_verilog(verilog_file)
        print(f"[Success] Loaded Design. Total cells instanced: {chip.get_instance_count()}")
    else:
        print(f"[Warning] {verilog_file} not found. Attempting to fall back to a benchmark.")
        fallback = "benchmarks/shift_reg.v"
        if os.path.exists(fallback):
            chip.load_verilog(fallback)
            print(f"[Success] Loaded Fallback Design. Total cells instanced: {chip.get_instance_count()}")
        else:
            print("[Error] No valid Verilog file found to test.")
            return

    # 2. Run Placement
    print("\n[2/4] Running Initial Placement & Legalization...")
    open_eda.run_placement(chip)
    
    # 3. Interactive Debugging!
    print(f"\n[Info] Placement Complete. Cells placed: {chip.get_instance_count()}")
    
    # 4. Try running Clock Tree Synthesis 
    print("\n[3/5] Running Clock Tree Synthesis (Method of Means and Medians)...")
    cts = open_eda.CtsEngine()
    
    # We try running it on "clk" which is standard in structural netlists
    cts.run_cts(chip, "clk")
    
    # 4. Synthesize Power Delivery Network (PDN)
    print("\n[4/7] Generating Power Delivery Network (M3/M4 Grids)...")
    pdn = open_eda.PdnGenerator(chip)
    pdn.run()
    
    # 5. Trigger Routing
    print("\n[5/7] Running Detailed 3D Routing Engine...")
    router = open_eda.RouteEngine()
    router.route(chip)
    
    # 6. Export GDSII Tape-Out
    print("\n[6/7] Exporting GDSII Tape-Out...")
    open_eda.export_gds("output.gds", chip)
    
    # 7. Open the Visualizer
    print("\n[7/7] Launching Hardware-Accelerated GUI...")
    gui = open_eda.GuiEngine()
    gui.show(chip, None, cts)
    
    print("\n=== Workflow Complete ===")

if __name__ == "__main__":
    main()
