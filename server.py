from fastapi import FastAPI, UploadFile, File, HTTPException
from fastapi.responses import FileResponse
import shutil
import os
import uuid
import sys
import importlib.util

def load_open_eda():
    pyd_path = os.path.join(os.path.dirname(__file__), 'build', 'Release', 'open_eda.cp313-win_amd64.pyd')
    if not os.path.exists(pyd_path):
        pyd_path = os.path.join(os.path.dirname(__file__), 'open_eda.cp313-win_amd64.pyd')
    
    spec = importlib.util.spec_from_file_location("open_eda", pyd_path)
    open_eda = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(open_eda)
    return open_eda

# Import your proprietary C++ EDA engine!
open_eda = load_open_eda()

app = FastAPI(
    title="Byteflow Cloud EDA API",
    description="Hardware-accelerated physical design engine by Sigma ByteCraft.",
    version="1.0.0"
)

# Create a temporary workspace for cloud file uploads
WORKSPACE_DIR = "cloud_workspace"
os.makedirs(WORKSPACE_DIR, exist_ok=True)

@app.post("/api/v1/flow/run_all")
async def run_full_physical_design_flow(netlist: UploadFile = File(...)):
    """
    Upload a synthesized .v file, get a routed .gds tape-out back.
    """
    if not netlist.filename.endswith('.v'):
        raise HTTPException(status_code=400, detail="Only Verilog (.v) files are supported.")

    # Setup unique workspace for this specific API request
    run_id = str(uuid.uuid4())[:8]
    input_v_path = os.path.join(WORKSPACE_DIR, f"input_{run_id}.v")
    output_gds_path = os.path.join(WORKSPACE_DIR, f"output_{run_id}.gds")
    output_spef_path = os.path.join(WORKSPACE_DIR, f"output_{run_id}.spef")

    # Save the user's uploaded file to the server disk
    with open(input_v_path, "wb") as buffer:
        shutil.copyfileobj(netlist.file, buffer)

    try:
        # --- THE BYTEFLOW C++ PIPELINE ---
        print(f"[{run_id}] Initializing Design Database...")
        chip = open_eda.Design()
        chip.load_verilog(input_v_path)

        # Floorplanning & Placement
        print(f"[{run_id}] Running Floorplanning & Placement...")
        floorplanner = open_eda.Floorplanner()
        floorplanner.place_macros(chip)
        open_eda.run_placement(chip)

        # Clock Tree
        print(f"[{run_id}] Synthesizing Clock Tree...")
        cts = open_eda.CtsEngine()
        cts.run_cts(chip, 'clk')

        # Power Grid
        print(f"[{run_id}] Generating Power Mesh...")
        pdn = open_eda.PdnGenerator(chip)
        pdn.run()

        # Routing
        print(f"[{run_id}] Running 3D A* Routing...")
        router = open_eda.RouteEngine()
        router.route(chip)

        # Extraction
        print(f"[{run_id}] Extracting RC Parasitics...")
        spef = open_eda.SpefEngine()
        spef.extract(chip)
        spef.write_spef(output_spef_path, chip)

        # Tape-Out
        print(f"[{run_id}] Exporting GDSII Tape-out...")
        open_eda.export_gds(output_gds_path, chip)

        # Return the physical GDSII mask back to the user's browser
        return FileResponse(
            path=output_gds_path, 
            media_type='application/octet-stream', 
            filename=f"{netlist.filename}_routed.gds"
        )

    except Exception as e:
        import traceback
        traceback.print_exc()
        raise HTTPException(status_code=500, detail=f"EDA Engine Failure: {str(e)}")

# Added main block so users can execute python server.py directly if they want
if __name__ == "__main__":
    import uvicorn
    uvicorn.run("server:app", host="127.0.0.1", port=8000, reload=True)
