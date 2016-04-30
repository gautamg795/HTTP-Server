# -*- mode: ruby -*-
# vi: set ft=ruby :

Vagrant.configure(2) do |config|
  config.vm.box = "ubuntu/trusty64"

  # Create a private network, which allows host-only access to the machine
  # using a specific IP.
   config.vm.network "private_network", ip: "192.168.10.10"

   config.vm.provider "virtualbox" do |vb|
     vb.memory = "1536"
   end
  config.vm.provision "shell", inline: <<-SHELL
    sudo add-apt-repository ppa:ubuntu-toolchain-r/test
    sudo apt-get update
    sudo apt-get install -y git build-essential gcc-5 g++-5 gdb
    sudo apt-get upgrade -y
    sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-5 60 --slave /usr/bin/g++ g++ /usr/bin/g++-5
  SHELL
end
