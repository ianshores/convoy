// SPDX-License-Identifier: MIT
// Headless smoke test of the built plugin, using caller-supplied CLAP headers.
#include <clap/entry.h>
#include <clap/plugin-factory.h>
#include <clap/host.h>
#include <clap/ext/params.h>
#include <dlfcn.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
static void check(bool ok,const char*message){if(!ok)throw std::runtime_error(message);}
static const void* extension(const clap_host_t*,const char*){return nullptr;}
static void request(const clap_host_t*){}
static uint32_t count(const clap_input_events_t*list){return list->ctx?1:0;}
static const clap_event_header_t* get(const clap_input_events_t*list,uint32_t){return static_cast<const clap_event_header_t*>(list->ctx);}
static bool push(const clap_output_events_t*,const clap_event_header_t*){return true;}
int main(int argc,char**argv){try{
 check(argc==2,"pass the CLAP path"); void*lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);check(lib,dlerror());
 const auto*entry=static_cast<const clap_plugin_entry_t*>(dlsym(lib,"clap_entry"));check(entry&&entry->init(argv[1]),"entry init");
 const auto*factory=static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));check(factory,"factory");
 clap_host_t host{CLAP_VERSION,nullptr,"heavyverb headless check","heavyverb","https://github.com/ianshores/heavyverb","1",extension,request,request,request};
 const auto*desc=factory->get_plugin_descriptor(factory,0);check(desc,"descriptor");
 const auto*plugin=factory->create_plugin(factory,&host,desc->id);check(plugin&&plugin->init(plugin),"plugin init");
 const auto*params=static_cast<const clap_plugin_params_t*>(plugin->get_extension(plugin,CLAP_EXT_PARAMS));check(params,"params extension");
 clap_param_info_t info{};bool found=false;
 for(uint32_t i=0;i<params->count(plugin);++i){check(params->get_info(plugin,i,&info),"parameter info");if(std::strstr(info.name,"Tail Cutoff")){found=true;break;}}
 check(found&&info.default_value==0&&info.min_value==-144&&info.max_value==0,"cutoff metadata");
 check(info.flags&CLAP_PARAM_IS_AUTOMATABLE,"automation flag");
 check(plugin->activate(plugin,48000,1,256)&&plugin->start_processing(plugin),"activate");
 clap_output_events_t outgoing{nullptr,push}; clap_input_events_t incoming{nullptr,count,get};
 float in0[256]{},in1[256]{},out0[256]{},out1[256]{};float*ins[]={in0,in1},*outs[]={out0,out1};
 clap_audio_buffer_t input{},output{};input.data32=ins;input.channel_count=2;output.data32=outs;output.channel_count=2;
 clap_process_t process{};process.frames_count=256;process.audio_inputs=&input;process.audio_outputs=&output;process.audio_inputs_count=process.audio_outputs_count=1;process.in_events=&incoming;process.out_events=&outgoing;
 auto render=[&](bool impulse){std::vector<float> result;for(int b=0;b<160;++b){in0[0]=in1[0]=impulse&&b==0?.5f:0.f;check(plugin->process(plugin,&process)!=CLAP_PROCESS_ERROR,"process");process.steady_time+=256;for(int i=0;i<256;++i){check(std::isfinite(out0[i])&&out0[i]==out1[i],"finite stereo output");result.push_back(out0[i]);}}return result;};
 auto set=[&](double value){clap_event_param_value_t event{};event.header.size=sizeof(event);event.header.space_id=CLAP_CORE_EVENT_SPACE_ID;event.header.type=CLAP_EVENT_PARAM_VALUE;event.param_id=info.id;event.cookie=info.cookie;event.note_id=event.port_index=event.channel=event.key=-1;event.value=value;incoming.ctx=&event.header;params->flush(plugin,&incoming,&outgoing);incoming.ctx=nullptr;render(false);double actual;check(params->get_value(plugin,info.id,&actual)&&actual==value,"parameter value");};
 auto full=render(true);check(std::abs(full[256]-.0625f)<1e-6,"full impulse gain/latency");
 set(-1);auto trimmed=render(true);set(0);auto restored=render(true);
 double change=0,restore_error=0;for(std::size_t i=0;i<full.size();++i){change=std::max(change,std::abs(double(trimmed[i])-full[i]));restore_error=std::max(restore_error,std::abs(double(restored[i])-full[i]));}
 check(change>1e-8&&restore_error<1e-7,"cutoff effect and restoration");
 plugin->stop_processing(plugin);plugin->deactivate(plugin);plugin->destroy(plugin);entry->deinit();dlclose(lib);
 std::printf("PASS: CLAP parameter range/default/automation, stereo render, live cutoff, restoration; changed peak %.9g, restore error %.9g\n",change,restore_error);
 }catch(const std::exception&e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
