// Trains the (768x10hm -> N)x2 -> 1 SCReLU net on bulletformat shards (eval-only target: no game results exist).
// Inputs: 10 king buckets, horizontally mirrored, plus a shared 768 "factoriser" folded into every bucket at save
// time (bullet examples/progression/3_input_buckets.rs). The engine's src/nnue.cpp mirrors KB_LAYOUT exactly.
// Usage: train <hidden: 128|256> <superbatches> <net_id> <data_dir> [measure]
use bullet_lib::{
    game::inputs::{ChessBucketsMirrored, get_num_buckets},
    nn::{InitSettings, Shape, optimiser::AdamW},
    trainer::{
        save::SavedFormat,
        schedule::{TrainingSchedule, TrainingSteps, lr, wdl},
        settings::{LocalSettings, TestDataset},
    },
    value::{ValueTrainerBuilder, loader},
};

#[rustfmt::skip]
const KB_LAYOUT: [usize; 32] = [
    0, 1, 2, 3,
    4, 4, 5, 5,
    6, 6, 6, 6,
    7, 7, 7, 7,
    8, 8, 8, 8,
    8, 8, 8, 8,
    9, 9, 9, 9,
    9, 9, 9, 9,
];
const NUM_KB: usize = get_num_buckets(&KB_LAYOUT);
const SCALE: i32 = 400;
const QA: i16 = 255;
const QB: i16 = 64;

// Six fixed positions (opening -> pawn endgame); their float evals become the engine's cross-check fixture.
const CHECK_FENS: [&str; 6] = [
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r1b2rk1/1p2bppp/p1nppn2/q7/2P1P3/N1N5/PP2BPPP/R1BQ1RK1 w - - 0 1",
    "1r2kb1r/pBp2ppp/4pn2/5b2/Q1pq4/6P1/PP1NPP1P/R1B2RK1 b k - 0 1",
    "8/p4pp1/3kp1p1/PPp1n3/2P1p3/4P2P/3KBPP1/8 b - - 0 1",
    "2r1r1k1/3Q1p2/7R/4N1p1/P4n2/1q5P/5PP1/4R1K1 w - - 0 1",
    "8/4k3/8/4K3/8/4P3/8/8 b - - 0 1",
];

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let hidden: usize = args[1].parse().expect("hidden size");
    let superbatches: usize = args[2].parse().expect("superbatches");
    let net_id = args[3].clone();
    let data_dir = args[4].clone();
    let measure = args.get(5).is_some_and(|a| a == "measure");

    let mut trainer = ValueTrainerBuilder::default()
        .dual_perspective()
        .optimiser(AdamW)
        .inputs(ChessBucketsMirrored::new(KB_LAYOUT))
        .save_format(&[
            SavedFormat::id("l0w")
                .transform(|store, weights| {
                    let factoriser = store.get("l0f").values.f32().repeat(NUM_KB);
                    weights.into_iter().zip(factoriser).map(|(a, b)| a + b).collect()
                })
                .round()
                .quantise::<i16>(QA),
            SavedFormat::id("l0b").round().quantise::<i16>(QA),
            SavedFormat::id("l1w").round().quantise::<i16>(QB),
            SavedFormat::id("l1b").round().quantise::<i16>(QA * QB),
        ])
        .loss_fn(|output, target| output.sigmoid().squared_error(target))
        .build(|builder, stm_inputs, ntm_inputs| {
            let l0f = builder.new_weights("l0f", Shape::new(hidden, 768), InitSettings::Zeroed);
            let mut l0 = builder.new_affine("l0", 768 * NUM_KB, hidden);
            l0.weights = l0.weights + l0f.repeat(NUM_KB);
            let l1 = builder.new_affine("l1", 2 * hidden, 1);
            let stm_hidden = l0.forward(stm_inputs).screlu();
            let ntm_hidden = l0.forward(ntm_inputs).screlu();
            l1.forward(stm_hidden.concat(ntm_hidden))
        });

    let schedule = TrainingSchedule {
        net_id: net_id.clone(),
        eval_scale: SCALE as f32,
        steps: TrainingSteps {
            batch_size: 16_384,
            batches_per_superbatch: if measure { 200 } else { 6104 },  // 6104 * 16384 ~ 100M positions
            start_superbatch: 1,
            end_superbatch: superbatches,
        },
        wdl_scheduler: wdl::ConstantWDL { value: 0.0 },  // labels are evals only
        lr_scheduler: lr::CosineDecayLR { initial_lr: 0.001, final_lr: 0.001 * 0.3 * 0.3 * 0.3, final_superbatch: superbatches },
        save_rate: 10,
    };

    let mut files: Vec<String> = std::fs::read_dir(&data_dir)
        .expect("data dir")
        .filter_map(|e| e.ok().map(|e| e.path().display().to_string()))
        .filter(|p| p.contains("/train_") && p.ends_with(".bin"))
        .collect();
    files.sort();
    let refs: Vec<&str> = files.iter().map(String::as_str).collect();
    let val = format!("{data_dir}/val.bin");
    let settings = LocalSettings {
        threads: 4,
        test_set: Some(TestDataset::at(&val)),
        output_directory: "checkpoints",
        batch_queue_size: 64,
    };
    let data_loader = loader::DirectSequentialDataLoader::new(&refs);
    trainer.run(&schedule, &settings, &data_loader);

    // Float reference evals (centipawns, side-to-move relative) for the engine's quantised cross-check.
    let mut out = String::new();
    for fen in CHECK_FENS {
        out += &format!("{fen}|{:.2}\n", trainer.eval(fen) * SCALE as f32);
    }
    std::fs::write(format!("checkpoints/{net_id}.evals.txt"), out).expect("write evals");
}
