import os
import glob
import pandas as pd

MIN_REPLY_RATE = 0.01

def get_loss(files):
    total_reply = 0
    total_timeout = 0
    for f in files:
        try:
            df = pd.read_csv(f)
            # Assuming scion_probe_*.csv has columns 'reply' and 'timeout'
            # Based on standard scion probe output in this repo
            replies = df['reply'].sum()
            timeouts = df['timeout'].sum()
            total = replies + timeouts
            if total > 0 and (replies / total) >= MIN_REPLY_RATE:
                total_reply += replies
                total_timeout += timeouts
        except:
            continue
    if (total_reply + total_timeout) == 0:
        return 0.0
    return 100.0 * total_timeout / (total_reply + total_timeout)

build_dir = "build"
parent_files = glob.glob(os.path.join(build_dir, "scion_probe_*.csv"))
num_parent = len(parent_files)

# Discover first 20 directories matching build/sweep_scion_* (depth 1)
all_dirs = sorted([d for d in glob.glob(os.path.join(build_dir, "sweep_scion_*")) if os.path.isdir(d)])
target_dirs = all_dirs[:20]

results = []
for d in target_dirs:
    run_files = glob.glob(os.path.join(d, "scion_probe_*.csv"))
    run_only_loss = get_loss(run_files)
    run_plus_parent_loss = get_loss(run_files + parent_files)
    results.append({
        'run_name': os.path.basename(d),
        'run_only': round(run_only_loss, 6),
        'run_plus_parent': round(run_plus_parent_loss, 6),
        'num_run_files': len(run_files),
        'num_parent_files': num_parent
    })

df_res = pd.DataFrame(results)
print(df_res.to_string(index=False))

print("\nUnique counts (rounded 6 decimals):")
print(f"run_only: {df_res['run_only'].nunique()}")
print(f"run_plus_parent: {df_res['run_plus_parent'].nunique()}")
