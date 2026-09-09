"""Reproducible extraction from pinned FunASR CLI into persistent in-process sessions.
The upstream snapshots and MIT license live in vendor/. Generated src files are committed.
"""
from pathlib import Path
import re
root = Path(__file__).resolve().parents[1]
source = (root / 'vendor/sensevoice_original.cpp').read_text(encoding='utf-8')
prefix = source[:source.index('int main(')]
prefix = prefix.replace('#define FUNASR_AUDIO_IMPLEMENTATION\n#include "funasr_audio.h"', '')
prefix = prefix.replace('#include "funasr_vad.h"', '// #include "funasr_vad.h"')
prefix = prefix.replace('#include "funasr_srt.h"', '')
prefix = prefix.replace('exit(1);', 'throw std::runtime_error("SenseVoice native initialization failure");')
prefix = '#include "engine.h"\n#include <stdexcept>\n' + prefix
load = source[source.index('  // load model\n'):source.index('  // Run encoder+CTC')]
load = load.replace('model m; ', '').replace('return 1;', 'throw std::runtime_error("Invalid SenseVoice model");')
load = load.replace('int qi=', 'int qi=').replace('int nq=', 'nq=')
load = load.replace('std::vector<int> qtok(nq);', 'qtok.resize(nq);')
load = load.replace('std::vector<std::string> vocab; ', '')
load = load.replace('const int F=560, D=m.c.d_model, V=m.c.vocab;', 'D=m.c.d_model; V=m.c.vocab;')
load = load.replace('bool emit_ids = ids_mode || vocab.empty();', 'if (vocab.empty()) throw std::runtime_error("Missing SenseVoice vocabulary");')
load = load.replace('std::vector<float> embed_f32((size_t)ggml_nelements(embed));', 'embed_f32.resize((size_t)ggml_nelements(embed));')
seg = source[source.index('    int N=nq+T;'):source.index('\n  int64_t t0=')]
seg = seg[:seg.rfind('  };')]
seg = seg.replace('(size_t)1024*1024*1024', '(size_t)64*1024*1024')
seg = seg.replace('ggml_context*c=ggml_init(cp);', 'ggml_context*c=ggml_init(cp);\n    if(!c) throw std::runtime_error("Graph metadata allocation failed");\n    auto ctx_guard=std::unique_ptr<ggml_context, decltype(&ggml_free)>(c, ggml_free);')
seg = seg.replace('ggml_gallocr_alloc_graph(ga,gf);', 'auto ga_guard=std::unique_ptr<ggml_gallocr, decltype(&ggml_gallocr_free)>(ga, ggml_gallocr_free);\n    if(!ggml_gallocr_alloc_graph(ga,gf)) throw std::runtime_error("Graph allocation failed");')
seg = seg.replace('graph_be.backend,8)', 'graph_be.backend,threads)')
seg = seg.replace('fprintf(stderr,"compute failed\\n");', 'throw std::runtime_error("SenseVoice compute failed");')
start = seg.index('    if(emit_ids)')
end = seg.index('    return result;', start)
seg = seg[:start] + '''    bool speech=false;
    for(int id:seg_ids) if(id>=0&&id<(int)vocab.size()) {
      if(vocab[id]=="<|nospeech|>") return {};
      if(vocab[id]=="<|Speech|>") speech=true;
    }
    if(!speech) return {};
    result=detok_sv(seg_ids,vocab,false);
''' + seg[end:]
text = prefix + '''
struct SenseVoice::Impl {
  model m;
  graph_backend graph_be;
  static constexpr int F=560;
  int D=0,V=0,nq=0,threads=8;
  std::vector<int> qtok;
  std::vector<std::string> vocab;
  std::vector<float> embed_f32;
  ~Impl(){free_model(m); if(graph_be.backend) ggml_backend_free(graph_be.backend);}
  void load(const std::string& gguf_path,int thread_count){
    threads=thread_count;
    graph_be=make_graph_backend("cpu");
''' + load + '\n  }\n  std::string run(const std::vector<float>& fb,int T){\n' + seg + '''
  }
};
SenseVoice::SenseVoice(const std::string& path,int threads):impl_(std::make_unique<Impl>()) { impl_->load(path,threads); }
SenseVoice::~SenseVoice()=default;
std::string SenseVoice::transcribe(const std::vector<float>& samples){
  if(samples.size()<400) return {};
  if(samples.size()>16000*30) throw std::runtime_error("ASR window exceeds 30 seconds");
  int frames=0;
  auto fb=compute_fbank(samples,frames);
  return impl_->run(fb,frames);
}
'''
(root/'src/sensevoice.cpp').write_text(text,encoding='utf-8')

vad=(root/'vendor/funasr_vad_original.h').read_text(encoding='utf-8')
pre=vad[:vad.index('// Run FSMN-VAD')].replace('#pragma once','')
load=vad[vad.index('  vad m;'):vad.index('  auto feat=fbank80')]
load=load.replace('vad m; ', '').replace('int idim=', 'idim=')
load=load.replace('return false;', 'throw std::runtime_error("Invalid FSMN-VAD model");')
load=load.replace('if(m.ctx)ggml_free(m.ctx); ', '')
calc=vad[vad.index('  auto feat=fbank80'):vad.index('  // ===== E2EVadModel')]
calc=calc.replace('if(m.ctx)ggml_free(m.ctx);return true;', 'return {};')
calc=calc.replace('  ggml_backend_t be=ggml_backend_cpu_init();', '')
calc=calc.replace('ggml_backend_cpu_set_n_threads(be,nthreads)', 'ggml_backend_cpu_set_n_threads(be,threads)')
calc=calc.replace('ggml_backend_free(be);if(m.ctx)ggml_free(m.ctx);', '')
calc=calc.replace('if(!ok)return false;', 'if(!ok) throw std::runtime_error("FSMN-VAD compute failed");\n  std::vector<float> probabilities(T);\n  for(int t=0;t<T;++t) probabilities[t]=1.0f-sc[(size_t)t*od];\n  return probabilities;')
text='#include "engine.h"\n#include <stdexcept>\n'+pre+'''
using namespace funasr_vad_impl;
struct SpeechVad::Impl {
  vad m;
  ggml_backend_t be=nullptr;
  int idim=0,pd=0,nl=0,lorder=0,od=0,lm=0,ln=0,threads=2;
  ~Impl(){if(be)ggml_backend_free(be);if(m.ctx)ggml_free(m.ctx);}
  void load(const std::string& gguf_path,int thread_count){
    threads=thread_count;
'''+load+'''
    be=ggml_backend_cpu_init();
    if(!be) throw std::runtime_error("FSMN-VAD CPU initialization failed");
  }
  std::vector<float> run(const std::vector<float>& wav){
'''+calc+'''
  }
};
SpeechVad::SpeechVad(const std::string& path,int threads):impl_(std::make_unique<Impl>()){impl_->load(path,threads);}
SpeechVad::~SpeechVad()=default;
std::vector<float> SpeechVad::probabilities(const std::vector<float>& samples){return impl_->run(samples);}
'''
(root/'src/vad.cpp').write_text(text,encoding='utf-8')
