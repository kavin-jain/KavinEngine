// Trains the (768x10hm -> N)x2 -> 1x8 SCReLU net on bulletformat shards (eval-only target: no game results exist).
// Inputs: 10 king buckets, horizontally mirrored, plus a shared 768 "factoriser" folded into every bucket at save
// time (bullet examples/progression/3_input_buckets.rs). The engine's src/nnue.cpp mirrors KB_LAYOUT exactly.
// Output: 8 buckets by piece count (MaterialCount<8>, engine NNUE_OUTPUT_BUCKETS=8).
// ARCH=ml: the multi-layer net instead (bullet examples/progression/4_multi_layer.rs): feature transformer -> CReLU
// pairwise product (N/2 per side) -> L1 N -> 16 (i8) -> SCReLU -> L2 16 -> 32 -> SCReLU -> L3 32 -> 1, per output bucket.
// Usage: train <hidden: 128|256> <superbatches> <net_id> <data_dir> [measure]
use bullet_lib::{
    game::{
        inputs::{ChessBucketsMirrored, get_num_buckets},
        outputs::MaterialCount,
    },
    nn::{InitSettings, Shape, optimiser::{AdamW, AdamWParams}},
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
const NUM_OB: usize = 8;
const SCALE: i32 = 400;
const QA: i16 = 255;
const QB: i16 = 64;
const QL1: i8 = 64;      // multi-layer net: L1 weights as i8 at this scale
const L2: usize = 16;    // multi-layer net: L1 outputs per output bucket (then 32, then 1)

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
    // Game-result weight in the label (needs data with real results, e.g. tools/datagen.cpp output).
    let wdl: f32 = std::env::var("WDL").map(|v| v.parse().expect("WDL")).unwrap_or(0.0);
    // Fine-tuning: start from a checkpoint directory (INIT) with its own initial learning rate (LR).
    let init_lr: f32 = std::env::var("LR").map(|v| v.parse().expect("LR")).unwrap_or(0.001);
    // Score units per our centipawn x 400: data labelled in other units (e.g. Stockfish-internal, Leela-derived) is
    // trained with EVAL_SCALE = 400 * (its WDL scale / ours), from `lichess_convert wdlfit`, so outputs stay in our cp.
    let eval_scale: f32 = std::env::var("EVAL_SCALE").map(|v| v.parse().expect("EVAL_SCALE")).unwrap_or(SCALE as f32);

    let schedule = TrainingSchedule {
        net_id: net_id.clone(),
        eval_scale,
        steps: TrainingSteps {
            batch_size: 16_384,
            batches_per_superbatch: if measure { 200 } else { 6104 },  // 6104 * 16384 ~ 100M positions
            start_superbatch: 1,
            end_superbatch: superbatches,
        },
        wdl_scheduler: wdl::ConstantWDL { value: wdl },
        lr_scheduler: lr::CosineDecayLR { initial_lr: init_lr, final_lr: init_lr * 0.3 * 0.3 * 0.3, final_superbatch: superbatches },
        save_rate: 10,
    };

    let mut files: Vec<String> = std::fs::read_dir(&data_dir)
        .expect("data dir")
        .filter_map(|e| e.ok().map(|e| e.path().display().to_string()))
        .filter(|p| p.contains("/train_") && p.ends_with(".bin"))
        .collect();
    files.sort();
    // Scaling-law study: train on the first SHARDS shards only (each ~7.4M Lichess positions); default all.
    if let Ok(n) = std::env::var("SHARDS") {
        files.truncate(n.parse().expect("SHARDS"));
    }
    let refs: Vec<&str> = files.iter().map(String::as_str).collect();
    let val = format!("{data_dir}/val.bin");
    let settings = LocalSettings {
        threads: 4,
        test_set: Some(TestDataset::at(&val)),
        output_directory: "checkpoints",
        batch_queue_size: 64,
    };
    let data_loader = loader::DirectSequentialDataLoader::new(&refs);
    // The rest is the same for both architectures (their trainers are different types).
    macro_rules! train_and_check {
        ($trainer:ident) => {{
            if let Ok(init) = std::env::var("INIT") {
                $trainer.load_from_checkpoint(&init);
            }
            $trainer.run(&schedule, &settings, &data_loader);
            // Float reference evals (centipawns, side-to-move relative) for the engine's quantised cross-check.
            let mut out = String::new();
            for fen in CHECK_FENS {
                out += &format!("{fen}|{:.2}\n", $trainer.eval(fen) * SCALE as f32);
            }
            std::fs::write(format!("checkpoints/{net_id}.evals.txt"), out).expect("write evals");
        }};
    }
    if std::env::var("ARCH").is_ok_and(|a| a == "ml") {
        let mut trainer = ValueTrainerBuilder::default()
            .dual_perspective()
            .optimiser(AdamW)
            .inputs(ChessBucketsMirrored::new(KB_LAYOUT))
            .output_buckets(MaterialCount::<NUM_OB>)
            .save_format(&[
                SavedFormat::id("l0w")
                    .transform(|store, weights| {
                        let factoriser = store.get("l0f").values.f32().repeat(NUM_KB);
                        weights.into_iter().zip(factoriser).map(|(a, b)| a + b).collect()
                    })
                    .round()
                    .quantise::<i16>(QA),
                SavedFormat::id("l0b").round().quantise::<i16>(QA),
                SavedFormat::id("l1w").transpose().round().quantise::<i8>(QL1),  // [bucket * 16 + j][N]
                SavedFormat::id("l1b"),              // f32 [bucket * 16 + j]
                SavedFormat::id("l2w").transpose(),  // f32 [bucket * 32 + k][16]
                SavedFormat::id("l2b"),              // f32 [bucket * 32 + k]
                SavedFormat::id("l3w").transpose(),  // f32 [bucket][32]
                SavedFormat::id("l3b"),              // f32 [bucket]
            ])
            .loss_fn(|output, target| output.sigmoid().squared_error(target))
            .build(|builder, stm_inputs, ntm_inputs, output_buckets| {
                let l0f = builder.new_weights("l0f", Shape::new(hidden, 768), InitSettings::Zeroed);
                let mut l0 = builder.new_affine("l0", 768 * NUM_KB, hidden);
                l0.init_with_effective_input_size(32);
                l0.weights = l0.weights + l0f.repeat(NUM_KB);
                let l1 = builder.new_affine("l1", hidden, NUM_OB * L2);
                let l2 = builder.new_affine("l2", L2, NUM_OB * 32);
                let l3 = builder.new_affine("l3", 32, NUM_OB);
                // CReLU pairwise product: neuron i times neuron i + N/2, per side.
                let ft = |input, a, b| l0.slice(a, b).forward(input).crelu();
                let stm_hidden = ft(stm_inputs, 0, hidden / 2) * ft(stm_inputs, hidden / 2, hidden);
                let ntm_hidden = ft(ntm_inputs, 0, hidden / 2) * ft(ntm_inputs, hidden / 2, hidden);
                let hl2 = l1.forward(stm_hidden.concat(ntm_hidden)).select(output_buckets).screlu();
                let hl3 = l2.forward(hl2).select(output_buckets).screlu();
                l3.forward(hl3).select(output_buckets)
            });
        // The factoriser is added to every bucket's weights at save time: clip both so the sum fits i16 at QA.
        let stricter = AdamWParams { max_weight: 0.99, min_weight: -0.99, ..Default::default() };
        trainer.optimiser.set_params_for_weight("l0w", stricter);
        trainer.optimiser.set_params_for_weight("l0f", stricter);
        train_and_check!(trainer);
    } else {
        let mut trainer = ValueTrainerBuilder::default()
            .dual_perspective()
            .optimiser(AdamW)
            .inputs(ChessBucketsMirrored::new(KB_LAYOUT))
            .output_buckets(MaterialCount::<NUM_OB>)
            .save_format(&[
                SavedFormat::id("l0w")
                    .transform(|store, weights| {
                        let factoriser = store.get("l0f").values.f32().repeat(NUM_KB);
                        weights.into_iter().zip(factoriser).map(|(a, b)| a + b).collect()
                    })
                    .round()
                    .quantise::<i16>(QA),
                SavedFormat::id("l0b").round().quantise::<i16>(QA),
                SavedFormat::id("l1w").round().quantise::<i16>(QB).transpose(),  // [bucket][2N]
                SavedFormat::id("l1b").round().quantise::<i16>(QA * QB),
            ])
            .loss_fn(|output, target| output.sigmoid().squared_error(target))
            .build(|builder, stm_inputs, ntm_inputs, output_buckets| {
                let l0f = builder.new_weights("l0f", Shape::new(hidden, 768), InitSettings::Zeroed);
                let mut l0 = builder.new_affine("l0", 768 * NUM_KB, hidden);
                l0.weights = l0.weights + l0f.repeat(NUM_KB);
                let l1 = builder.new_affine("l1", 2 * hidden, NUM_OB);
                let stm_hidden = l0.forward(stm_inputs).screlu();
                let ntm_hidden = l0.forward(ntm_inputs).screlu();
                l1.forward(stm_hidden.concat(ntm_hidden)).select(output_buckets)
            });
        train_and_check!(trainer);
    }
}
