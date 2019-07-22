cd('~/mycloud/eth/6th/msc_thesis/thesis/acquire_fingerprints');
files = dir('log');
titles = [];
bandwidths = [];
for k=1:length(files)
   fileName = files(k).name;
   if(strcmp(fileName, ".") == 0 && strcmp(fileName, "..") == 0)
       id = strsplit(fileName, '_');
       titles = [titles, id(1)];
       bandwidths = [bandwidths, id(2)];
   end
   
end

bandwidths = unique(bandwidths);
titles = unique(titles);

cd('data');
for k=1:length(titles)
%     files = dir(sprintf('%s*.dat', titles{k}));
    files = dir('80141858*.dat');

    names = {files.name};
    sorted = natsortfiles(names);
    disp(sorted);
    figure
    errs = [];
    for j=1:length(sorted)
        filename = char(sorted(j));
        disp(filename);
        disp(class(filename));
        data = importdata(filename);
%         x = data(:,1);
        y = data(:,2);
        x = (0:numel(y) -1);
        % Try to normalize y
        orig = y;
        y = normalize(y);
        
        win_size = 9;
        num_win = numel(y) - win_size;
%         disp(num_win);
        err = [];
        for i=1:num_win
            Fs = 1000;
            T = 1/Fs;
            L = win_size;
            t = (0:L-1) * T;
            % normalize the amplitudes 
            Fy = fft(normalize(y(i:i + L)));
            
            P2 = abs(Fy/L);
            P1 = P2(1:L/2+1);
            P1(2:end-1) = 2*P1(2:end-1);
            
%             disp(mean(P1));
            mean_P1 = repelem(mean(P1), length(P1)).';
%             disp(size(P1));
%             disp(size(mean_P1));
%             disp(meanvector);
            mse = mean((P1 - mean_P1).^2);
            err = [err , mse];
%             disp(mse);
            
            
%             f = Fs*(0:(L/2))/L;
%             ax(j) = subplot(num_win, 1, j);
%             plot(f, P1);
            
        end
        ax(j) = subplot(length(files), 1, j);
        plot(pwelch(orig));
%         disp(size(err));
%         plot(err.');
%         disp(err);
%         disp(err);
%         disp(y);
%         ax(j) = subplot(length(files), 1, j);
%         plot(x, y);
           
    end
    if(k == 1)
        break;
    end
end

